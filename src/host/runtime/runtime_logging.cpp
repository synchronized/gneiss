// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime_logging.hpp"
#include <cstdio>
#include <cstdlib>
#include <gneiss/app/runtime_log_protocol.hpp>
#include <system_error>
namespace gneiss::host {
namespace {
class runtime_protocol_sink final : public logging::sink {
public:
  error write(const logging::record_view& event) override {
    std::string line;
    const auto result = encode_runtime_log_event(event, line);
    if (result.failed())
      return result;
    auto* output = event.message.severity >= logging::level::warning ? stderr : stdout;
    if (std::fwrite(line.data(), 1, line.size(), output) != line.size())
      return error::io;
    return std::fflush(output) == 0 ? error::success : error::io;
  }
  error flush() override {
    const auto out = std::fflush(stdout);
    const auto err = std::fflush(stderr);
    return out == 0 && err == 0 ? error::success : error::io;
  }
};
[[nodiscard]] std::filesystem::path environment_path(const char* name) {
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

}
std::unique_ptr<logging::sink> create_runtime_protocol_sink() {
  return std::make_unique<runtime_protocol_sink>();
}
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

} // namespace gneiss::host
