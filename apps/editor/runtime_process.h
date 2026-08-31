// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_PROCESS_H_
#define GNEISS_APPS_EDITOR_RUNTIME_PROCESS_H_

#include "runtime_launch.h"

#include <memory>
#include <string>

namespace gneiss::editor {

class runtime_process final {
public:
  runtime_process();
  ~runtime_process();

  runtime_process(const runtime_process&) = delete;
  runtime_process& operator=(const runtime_process&) = delete;

  [[nodiscard]] result start(const std::filesystem::path& executable,
                             const runtime_launch_request& request) noexcept;
  [[nodiscard]] result request_stop() noexcept;
  void update() noexcept;

  [[nodiscard]] bool is_running() const noexcept;
  [[nodiscard]] bool has_started() const noexcept;
  [[nodiscard]] int exit_code() const noexcept;
  [[nodiscard]] const std::string& output() const noexcept;
  [[nodiscard]] const std::filesystem::path& log_file() const noexcept;
  void clear_output() noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::editor

#endif
