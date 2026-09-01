// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_log.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

namespace gneiss::runtime_internal {
namespace {

constexpr std::uintmax_t maximum_log_size = 1024U * 1024U;

[[nodiscard]] std::string format_event(std::string_view level, std::string_view stage,
                                       gneiss_result operation, std::string_view message,
                                       std::string_view context) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  std::string event = "time_ms=";
  event.append(std::to_string(timestamp));
  event.append(" level=");
  event.append(level);
  event.append(" process=runtime stage=");
  event.append(stage);
  event.append(" result=");
  event.append(std::to_string(operation));
  event.push_back(' ');
  event.append("message=\"");
  event.append(message);
  event.append("\" context=\"");
  event.append(context);
  event.append("\"\n");
  return event;
}

void append_quoted(std::string& output, std::string_view value) {
  output.push_back('"');
  for (const char character : value) {
    switch (character) {
    case '\\':
      output.append("\\\\");
      break;
    case '"':
      output.append("\\\"");
      break;
    case '\n':
      output.append("\\n");
      break;
    case '\r':
      output.append("\\r");
      break;
    case '\t':
      output.append("\\t");
      break;
    default:
      output.push_back(character);
      break;
    }
  }
  output.push_back('"');
}

[[nodiscard]] std::string_view severity_name(std::uint32_t severity) noexcept {
  switch (severity) {
  case GNEISS_LOG_TRACE:
    return "TRACE";
  case GNEISS_LOG_DEBUG:
    return "DEBUG";
  case GNEISS_LOG_INFO:
    return "INFO";
  case GNEISS_LOG_WARNING:
    return "WARNING";
  case GNEISS_LOG_ERROR:
    return "ERROR";
  case GNEISS_LOG_FATAL:
    return "FATAL";
  default:
    return "UNKNOWN";
  }
}

[[nodiscard]] std::string format_structured_event(const gneiss_log_event& source) {
  std::string event = "time_ns=";
  event.append(std::to_string(source.timestamp_ns));
  event.append(" sequence=");
  event.append(std::to_string(source.sequence));
  event.append(" level=");
  event.append(severity_name(source.severity));
  event.append(" process=runtime source=");
  append_quoted(event, {source.source, source.source_length});
  event.append(" category=");
  append_quoted(event, {source.category, source.category_length});
  event.append(" thread=");
  event.append(std::to_string(source.thread_id));
  event.append(" result=");
  event.append(std::to_string(source.result));
  event.append(" message=");
  append_quoted(event, {source.message, source.message_length});
  event.push_back('\n');
  return event;
}

void rotate_log(const std::filesystem::path& path) noexcept {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error ||
      std::filesystem::file_size(path, error) < maximum_log_size || error) {
    return;
  }
  auto backup = path;
  backup += ".1";
  std::filesystem::remove(backup, error);
  error.clear();
  std::filesystem::rename(path, backup, error);
}

[[nodiscard]] std::filesystem::path environment_path(const char* name) noexcept {
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
    return {};
  }
  std::filesystem::path result(value);
  std::free(value);
  return result;
#else
  const auto* value = std::getenv(name);
  return value == nullptr || value[0] == '\0' ? std::filesystem::path{} : value;
#endif
}

} // namespace

runtime_log::runtime_log(const std::filesystem::path& path) : path_(path) {
  std::error_code error;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) {
      return;
    }
  }
  rotate_log(path_);
  file_.open(path_, std::ios::out | std::ios::app);
}

void runtime_log::write(std::string_view level, std::string_view stage, gneiss_result operation,
                        std::string_view message, std::string_view context) noexcept {
  try {
    const std::scoped_lock lock(mutex_);
    const auto event = format_event(level, stage, operation, message, context);
    auto* stream = level == "INFO" ? stdout : stderr;
    std::fwrite(event.data(), sizeof(char), event.size(), stream);
    std::fflush(stream);

    if (!file_.is_open() || has_reported_write_failure_) {
      return;
    }
    file_ << event;
    file_.flush();
    if (!file_) {
      has_reported_write_failure_ = true;
      constexpr std::string_view warning =
          "level=WARNING process=runtime stage=log_file result=-1 "
          "message=\"日志文件写入失败，继续使用控制台日志\" context=\"\"\n";
      std::fwrite(warning.data(), sizeof(char), warning.size(), stderr);
      std::fflush(stderr);
    }
  } catch (...) {
    constexpr std::string_view warning = "level=WARNING process=runtime stage=log_format result=-1 "
                                         "message=\"日志事件格式化失败\" context=\"\"\n";
    std::fwrite(warning.data(), sizeof(char), warning.size(), stderr);
    std::fflush(stderr);
  }
}

void runtime_log::write(const gneiss_log_event& source) noexcept {
  try {
    const std::scoped_lock lock(mutex_);
    const auto event = format_structured_event(source);
    auto* stream = source.severity < GNEISS_LOG_WARNING ? stdout : stderr;
    std::fwrite(event.data(), sizeof(char), event.size(), stream);
    std::fflush(stream);

    if (!file_.is_open() || has_reported_write_failure_) {
      return;
    }
    file_ << event;
    file_.flush();
    if (!file_) {
      has_reported_write_failure_ = true;
      constexpr std::string_view warning =
          "level=WARNING process=runtime stage=log_file result=-1 "
          "message=\"日志文件写入失败，继续使用控制台日志\" context=\"\"\n";
      std::fwrite(warning.data(), sizeof(char), warning.size(), stderr);
      std::fflush(stderr);
    }
  } catch (...) {
    constexpr std::string_view warning = "level=WARNING process=runtime stage=log_format result=-1 "
                                         "message=\"日志事件格式化失败\" context=\"\"\n";
    std::fwrite(warning.data(), sizeof(char), warning.size(), stderr);
    std::fflush(stderr);
  }
}

bool runtime_log::is_file_available() const noexcept { return file_.is_open(); }

const std::filesystem::path& runtime_log::path() const noexcept { return path_; }

std::filesystem::path default_runtime_log_path() {
#if defined(_WIN32)
  auto state_root = environment_path("LOCALAPPDATA");
  if (!state_root.empty()) {
    return state_root / "Gneiss" / "logs" / "runtime.log";
  }
#else
  auto state_root = environment_path("XDG_STATE_HOME");
  if (!state_root.empty()) {
    return state_root / "gneiss" / "logs" / "runtime.log";
  }
  state_root = environment_path("HOME");
  if (!state_root.empty()) {
    return state_root / ".local" / "state" / "gneiss" / "logs" / "runtime.log";
  }
#endif
  std::error_code error;
  auto temporary = std::filesystem::temp_directory_path(error);
  return (error ? std::filesystem::path{"."} : temporary) / "Gneiss" / "logs" / "runtime.log";
}

} // namespace gneiss::runtime_internal
