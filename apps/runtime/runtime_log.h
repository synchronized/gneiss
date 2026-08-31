// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/core/result.h>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace gneiss::runtime_internal {

class runtime_log final {
public:
  explicit runtime_log(const std::filesystem::path& path);

  runtime_log(const runtime_log&) = delete;
  runtime_log& operator=(const runtime_log&) = delete;

  void write(std::string_view level, std::string_view stage, gneiss_result operation,
             std::string_view message, std::string_view context = {}) noexcept;

  [[nodiscard]] bool is_file_available() const noexcept;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
  std::filesystem::path path_;
  std::ofstream file_;
  bool has_reported_write_failure_ = false;
};

[[nodiscard]] std::filesystem::path default_runtime_log_path();

} // namespace gneiss::runtime_internal
