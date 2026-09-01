// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_PROCESS_CHILD_PROCESS_H_
#define GNEISS_SRC_PROCESS_CHILD_PROCESS_H_

#include <gneiss/core/result.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss {

struct child_process_start_info final {
  std::filesystem::path executable;
  std::vector<std::filesystem::path> arguments;
  std::filesystem::path working_directory;
};

// 供 Gneiss 内部工具使用的单子进程控制器；调用方负责外部同步。
class child_process final {
public:
  child_process();
  ~child_process();

  child_process(const child_process&) = delete;
  child_process& operator=(const child_process&) = delete;

  [[nodiscard]] result start(const child_process_start_info& info) noexcept;
  [[nodiscard]] result terminate() noexcept;
  void update() noexcept;

  [[nodiscard]] bool is_running() const noexcept;
  [[nodiscard]] bool has_started() const noexcept;
  [[nodiscard]] int exit_code() const noexcept;
  [[nodiscard]] const std::string& output() const noexcept;
  /** 取走自上次调用以来新增的输出，不影响 output() 的滚动历史。 */
  void consume_output(std::string& output) noexcept;
  void clear_output() noexcept;
  void append_output(std::string_view text) noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss

#endif
