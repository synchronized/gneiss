// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset_browser_model.hpp"

#include <gneiss/engine/core/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct ImGuiTextFilter;

namespace gneiss::editor {

enum class asset_browser_action {
  none,
  restart_source_watch,
  restart_author_watch,
  scan_sources,
  scan_author_assets,
  refresh,
  import_asset,
  reimport_selected,
  cancel_tasks,
  retry_import,
  cancel_scene_load,
  retry_scene_load,
  load_scene,
  cancel_runtime_reload,
  retry_runtime_reload,
  add_mesh,
  add_prefab,
  apply_to_node,
};

/** 绘制结束后由宿主执行，拥有当时的选择身份；不借用模型条目。 */
struct asset_browser_request final {
  asset_browser_action action = asset_browser_action::none;
  std::string asset_id;
};

enum class asset_status_tone { normal, success, error };

struct asset_watch_view final {
  std::string_view root;
  result operation = result::success;
  std::uint64_t dropped = 0U;
  bool waiting = false;
};

struct asset_budget_view final {
  std::uint64_t candidate_logical = 0U;
  std::uint64_t candidate_cpu = 0U;
  std::uint64_t application_logical = 0U;
  std::uint64_t application_cpu = 0U;
  std::uint64_t available = 0U;
  std::uint64_t upload = 0U;
  std::uint64_t peak_upload = 0U;
};

struct asset_scene_load_view final {
  bool enabled = false;
  bool visible = false;
  std::string_view phase;
  std::string_view uri;
  std::string_view message;
  std::uint32_t completed = 0U;
  std::uint32_t total = 0U;
  bool can_cancel = false;
  bool can_retry = false;
  bool can_load = false;
  std::optional<asset_budget_view> budget;
};

struct asset_reload_view final {
  result publish_result = result::success;
  bool visible = false;
  asset_status_tone tone = asset_status_tone::normal;
  std::uint64_t revision = 0U;
  std::string_view message;
  std::uint32_t completed = 0U;
  std::uint32_t total = 0U;
  bool can_cancel = false;
  bool can_retry = false;
  bool restart_required = false;
};

/** 主线程单次绘制借用；所有文本在返回前有效。只有显示值，不持有服务或进程状态。
 * 面板返回请求，宿主必须在绘制结束后再执行，避免失效借用的状态和文本。 */
struct asset_browser_view final {
  std::array<asset_watch_view, 2> watches;
  bool source_rescanning = false;
  bool author_rescanning = false;
  result source_scan_result = result::success;
  result author_scan_result = result::success;
  std::string_view worker_stage;
  std::string_view worker_source;
  std::string_view worker_error;
  std::size_t queued = 0U;
  bool can_cancel_tasks = false;
  bool refresh_failed = false;
  bool import_attempted = false;
  bool import_succeeded = false;
  bool can_retry_import = false;
  std::string_view import_diagnostic;
  asset_scene_load_view scene;
  asset_reload_view reload;
  bool author_change_visible = false;
  asset_status_tone author_change_tone = asset_status_tone::normal;
  std::string_view author_uri;
  std::string_view author_message;
  bool can_add_mesh = false;
  bool can_add_prefab = false;
  bool can_apply = false;
  bool scene_edit_attempted = false;
  result scene_edit_result = result::success;
};

[[nodiscard]] asset_browser_request draw_asset_browser_panel(asset_browser_model& model,
                                                             ImGuiTextFilter& filter, bool& open,
                                                             const asset_browser_view& view);

} // namespace gneiss::editor
