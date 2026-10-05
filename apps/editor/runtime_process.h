// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_RUNTIME_PROCESS_H_
#define GNEISS_APPS_EDITOR_RUNTIME_PROCESS_H_

#include "runtime_launch.hpp"

#include "console_model.hpp"
#include "ipc_property_protocol.h"
#include "ipc_scene_protocol.h"
#include "ipc_statistics_protocol.h"
#include "runtime_property_edits.hpp"
#include "runtime_scene_mirror.hpp"

#include <gneiss/app/project_description.h>

#include <cstdint>
#include <memory>
#include <span>
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

enum class runtime_asset_reload_state : std::uint8_t {
  idle,
  waiting,
  applying,
  applied,
  failed,
  restart_required,
};

struct runtime_asset_reload_status final {
  runtime_asset_reload_state state{runtime_asset_reload_state::idle};
  std::uint64_t revision = 0U;
  std::string message;
  result publish_result = result::success;
  bool can_cancel = false;
  std::uint32_t completed_assets = 0U;
  std::uint32_t total_assets = 0U;
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
  [[nodiscard]] result request_property_write(runtime_property_key key,
                                              std::uint64_t expected_revision,
                                              ipc_property_value value) noexcept;
  /** 发布已提交的派生资产；Runtime 未连接时保留到下次全量重同步。 */
  [[nodiscard]] result publish_asset_revision(std::span<const std::string> output_uris) noexcept;
  /** 失败后全量同步已知资产；执行中或未连接时返回 not_ready。 */
  [[nodiscard]] result cancel_asset_reload() noexcept;
  [[nodiscard]] result retry_asset_reload() noexcept;
  [[nodiscard]] result load_scene(std::string_view uri) noexcept;
  [[nodiscard]] result cancel_scene_load() noexcept;
  [[nodiscard]] result retry_scene_load() noexcept;
  [[nodiscard]] bool supports_scene_loading() const noexcept;
  [[nodiscard]] const ipc_scene_progress& scene_load_status() const noexcept;
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
  [[nodiscard]] const runtime_scene_mirror& scene_mirror() const noexcept;
  [[nodiscard]] const ipc_runtime_statistics& statistics() const noexcept;
  [[nodiscard]] const runtime_property_edit*
  property_edit(const runtime_property_key& key) const noexcept;
  [[nodiscard]] bool supports_property_editing() const noexcept;
  [[nodiscard]] const runtime_asset_reload_status& asset_reload_status() const noexcept;
  [[nodiscard]] const std::filesystem::path& log_file() const noexcept;
  [[nodiscard]] result last_result() const noexcept;
  void clear_output() noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> implementation_;
};

} // namespace gneiss::editor

#endif
