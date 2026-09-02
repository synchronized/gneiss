// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_PROCESS_H_
#define GNEISS_APPS_EDITOR_RUNTIME_PROCESS_H_

#include "runtime_launch.h"

#include "console_model.h"

#include <gneiss/app/project_description.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gneiss::editor {

enum class runtime_control_state : std::uint8_t {
  stopped,
  building,
  connecting,
  running,
  paused,
  stopping,
  failed,
};

class runtime_process final {
public:
  runtime_process();
  ~runtime_process();

  runtime_process(const runtime_process&) = delete;
  runtime_process& operator=(const runtime_process&) = delete;

  [[nodiscard]] result start(const std::filesystem::path& executable,
                             const runtime_launch_request& request) noexcept;
  [[nodiscard]] result build_and_start(const std::filesystem::path& cmake_executable,
                                       const std::filesystem::path& runtime_executable,
                                       const runtime_launch_request& request,
                                       const app::project_description& project) noexcept;
  [[nodiscard]] result request_stop() noexcept;
  [[nodiscard]] result request_pause() noexcept;
  [[nodiscard]] result request_resume() noexcept;
  void update() noexcept;

  [[nodiscard]] bool is_running() const noexcept;
  [[nodiscard]] bool is_building() const noexcept;
  [[nodiscard]] bool is_busy() const noexcept;
  [[nodiscard]] runtime_control_state control_state() const noexcept;
  [[nodiscard]] bool received_shutdown_complete() const noexcept;
  [[nodiscard]] bool has_started() const noexcept;
  [[nodiscard]] int exit_code() const noexcept;
  [[nodiscard]] const std::string& output() const noexcept;
  [[nodiscard]] const console_model& console() const noexcept;
  [[nodiscard]] const std::filesystem::path& log_file() const noexcept;
  [[nodiscard]] result last_result() const noexcept;
  void clear_output() noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::editor

#endif
