// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_application.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"

#include "child_process.hpp"
#include "commands/author_file_save.hpp"
#include "commands/author_property_edit.hpp"
#include "commands/editor_asset_edit.hpp"
#include "commands/editor_command_history.hpp"
#include "commands/editor_prefab_edit.hpp"
#include "commands/prefab_authoring.hpp"
#include "commands/runtime_author_apply.hpp"
#include "documents/document_editing.hpp"
#include "documents/editor_scene_loader.hpp"
#include "documents/scene_document_snapshot.hpp"
#include "editor_project.hpp"
#include "editor_theme.hpp"
#include "editor_ui.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "imgui_adapter.hpp"
#include "native_dialog.hpp"
#include "panels/author_hierarchy_panel.hpp"
#include "panels/console_panel.hpp"
#include "panels/document_action_panel.hpp"
#include "panels/material_editor_panel.hpp"
#include "panels/reflected_property_editor.hpp"
#include "panels/runtime_panels.hpp"
#include "project_manager.hpp"
#include "project_workspace.hpp"
#include "runtime_launch.hpp"
#include "runtime_process.hpp"
#include "viewport/editor_camera.hpp"
#include "viewport/editor_grid.hpp"
#include "viewport/editor_rotation_math.hpp"
#include "viewport/transform_gizmo_drag.hpp"
#include "viewport/transform_gizmo_math.hpp"
#include "viewport/transform_gizmo_view.hpp"
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
#include "asset_background_worker.hpp"
#include "asset_browser_model.hpp"
#include "asset_file_watcher.hpp"
#include "asset_import_controller.hpp"
#include "author_asset_service.hpp"
#include "panels/asset_browser_panel.hpp"
#endif

#include "engine/core/error.hpp"
#include "runtime/application.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class hierarchy_action { none, rename, duplicate, remove };
using gizmo_operation = gneiss::editor::transform_gizmo_operation;
enum class author_selection_kind : std::uint8_t { none, scene_node, prefab_root, prefab_source };
enum class prefab_author_action : std::uint8_t { none, create, apply, unpack };

struct author_selection final {
  author_selection_kind kind = author_selection_kind::none;
  gneiss::engine::guid primary_uuid;
  gneiss::engine::guid source_uuid;
};

struct author_asset_document final {
  std::string relative_path;
  std::string content;
};

struct author_scene_document final {
  std::string relative_path;
  std::string content;
};

struct editor_state {
  // 宿主拥有对象；只在其 run 回调期间借用。
  gneiss::runtime::application* application{};
  explicit editor_state(bool cooperative = false)
      : task_scheduler({.mode = cooperative ? gneiss::engine::execution_mode::cooperative
                                            : gneiss::engine::execution_mode::thread_pool}) {}
  gneiss::editor::imgui_adapter ui;
  gneiss::editor::editor_camera camera;
  gneiss::engine::task_scheduler task_scheduler;
  gneiss::editor::editor_scene_loader scene_loader;
  bool smoke{};
  std::uint32_t smoke_frames{};
  std::uint32_t smoke_frame_limit{3U};
  std::uint64_t smoke_presented_start{};
  std::chrono::steady_clock::time_point smoke_scene_started{};
  std::chrono::steady_clock::time_point smoke_started{std::chrono::steady_clock::now()};
  gneiss::editor::document_editing editing;
  gneiss::editor::material_editor_model material_editor;
  bool material_editor_visible = false;
  std::uint64_t material_edit_serial = 0;
  gneiss::engine::error material_edit_status = gneiss::engine::error::success;
  gneiss::engine::world* world = nullptr;
  gneiss::editor::runtime_scene_selection runtime_selection;
  gneiss::engine::error inspector_error = gneiss::engine::error::success;
  gneiss::engine::error history_error = gneiss::engine::error::success;
  gneiss::engine::scene_diagnostic prefab_author_diagnostic;
  gneiss::engine::error prefab_author_result = gneiss::engine::error::success;
  bool prefab_author_attempted = false;
  bool prefab_author_busy = false;
  // 仅宿主轮询回执并访问编辑状态；文件回调不捕获 editor_state。
  std::function<bool(gneiss::engine::error&)> pending_file_operation;
  bool file_operation_saves_scene{};
  bool run_after_save{};
  std::array<char, 192> prefab_path_buffer{};
  prefab_author_action pending_prefab_author_action = prefab_author_action::none;
  gneiss::engine::guid pending_prefab_instance_uuid;
  gneiss::engine::guid pending_prefab_source_uuid;
  gneiss::engine::guid pending_prefab_root_uuid;
  std::filesystem::path asset_root;
  std::filesystem::path project_root;
  gneiss::engine::error save_result = gneiss::engine::error::success;
  bool save_attempted = false;
  gneiss::host::runtime_process runtime;
  gneiss::engine::error runtime_result = gneiss::engine::error::success;
  bool runtime_attempted = false;
  gneiss::engine::child_process package_process;
  gneiss::engine::error package_result = gneiss::engine::error::success;
  bool package_attempted = false;
  bool show_package_dialog = false;
  bool package_zip = true;
  int package_profile = 1;
  std::array<char, 512> package_output{};
  gneiss::editor::console_panel_state console_panel;
  bool pending_save_and_run = false;
  bool show_imgui_demo = false;
  gneiss::editor::editor_panel_visibility panel_visibility;
  gneiss::editor::editor_layout layout;
  bool closing{};
  bool layout_save_started{};
  gneiss::engine::error layout_status{gneiss::engine::error::success};
  gizmo_operation gizmo_mode = gizmo_operation::translate;
  gneiss::editor::transform_gizmo_drag gizmo_drag;
  bool gizmo_wait_release = false;
  std::uint64_t property_edit_serial = 0U;
  std::array<char, 128> rename_buffer{};
  gneiss::engine::guid rename_uuid;
  std::string rename_previous;
  hierarchy_action pending_hierarchy_action = hierarchy_action::none;
  gneiss::engine::guid pending_hierarchy_uuid;
  gneiss::editor::document_actions document_actions;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  gneiss::editor::asset_browser_model assets;
  gneiss::editor::asset_file_watcher asset_watcher;
  std::chrono::steady_clock::time_point next_asset_watch_attempt{};
  bool asset_watch_failed = false;
  gneiss::engine::error asset_watch_result = gneiss::engine::error::success;
  gneiss::engine::error author_watch_result = gneiss::engine::error::success;
  gneiss::editor::asset_file_watcher author_asset_watcher;
  gneiss::editor::author_asset_service author_assets{task_scheduler};
  gneiss::editor::asset_background_worker asset_reimports{{}, &task_scheduler};
  std::deque<gneiss::editor::asset_file_event> pending_author_events;
  std::optional<std::string> author_reload_uri;
  std::size_t observed_author_drops = 0U;
  std::size_t observed_source_drops = 0U;
  std::size_t observed_candidate_drops = 0U;
  gneiss::editor::asset_browser_result asset_result = gneiss::editor::asset_browser_result::success;
  ImGuiTextFilter asset_filter;
  gneiss::editor::editor_import_report last_import;
  bool import_attempted = false;
  gneiss::engine::error asset_scene_result = gneiss::engine::error::success;
  bool asset_scene_attempted = false;
#endif
};

#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
void start_source_asset_watch(editor_state& state) {
  const auto now = std::chrono::steady_clock::now();
  if (state.asset_watcher.is_running() || state.asset_watch_failed ||
      now < state.next_asset_watch_attempt) {
    return;
  }
  state.next_asset_watch_attempt = now + std::chrono::seconds{1};
  const auto operation = state.asset_watcher.start(state.project_root / "sources", true);
  state.asset_watch_result = operation;
  if (operation == gneiss::engine::error::not_ready) {
    return;
  }
  if (operation == gneiss::engine::error::success) {
    state.asset_reimports.request_rescan();
    state.asset_reimports.request_refresh();
    return;
  }
  state.asset_watch_failed = true;
  const auto message = operation.message();
  std::fprintf(stderr, "Gneiss Editor 资产监听启动失败：结果=%d，消息=%.*s\n", operation.code(),
               static_cast<int>(message.size()), message.data());
}
#endif

constexpr std::size_t matrix_index(std::size_t row, std::size_t column) noexcept {
  return (column * 4U) + row;
}

gneiss::engine::error submit_editor_ui(editor_state& state) {
  const auto& settings = state.camera.settings();
  const auto& transform = state.camera.current_transform();
  const gneiss::engine::view_camera view{
      .camera = {settings.fov, settings.near, settings.far},
      .transform = {transform.translation, transform.rotation, transform.scale}};
  const auto operation = state.application->set_view_camera(&view);
  return operation == gneiss::engine::error::success ? state.ui.submit(*state.application)
                                                     : operation;
}

gneiss::engine::error submit_editor_grid(const editor_state& state) {
  const auto* viewport = ImGui::GetMainViewport();
  const auto& camera = state.camera.current_transform();
  auto lines =
      gneiss::editor::build_editor_grid(camera, gneiss::editor::build_gizmo_view_matrix(camera),
                                        bool(viewport) ? viewport->Size.y : 0.0F);
  lines.push_back({.start = {0.0F, 0.0F, 0.0F},
                   .end = {2.0F, 0.0F, 0.0F},
                   .color_rgba8 = IM_COL32(243, 139, 168, 255),
                   .width = 2.0F,
                   .depth_test = true});
  lines.push_back({.start = {0.0F, 0.0F, 0.0F},
                   .end = {0.0F, 2.0F, 0.0F},
                   .color_rgba8 = IM_COL32(166, 227, 161, 255),
                   .width = 2.0F,
                   .depth_test = true});
  lines.push_back({.start = {0.0F, 0.0F, 0.0F},
                   .end = {0.0F, 0.0F, 2.0F},
                   .color_rgba8 = IM_COL32(137, 180, 250, 255),
                   .width = 2.0F,
                   .depth_test = true});
  return state.application->submit_debug_draw_list(lines);
}

void draw_view_axis(const editor_state& state, const ImVec2& minimum, const ImVec2& size) noexcept {
  const auto view = gneiss::editor::build_gizmo_view_matrix(state.camera.current_transform());
  const ImVec2 center{minimum.x + size.x - 54.0F, minimum.y + 48.0F};
  constexpr float length = 28.0F;
  constexpr std::array colors{IM_COL32(243, 139, 168, 255), IM_COL32(166, 227, 161, 255),
                              IM_COL32(137, 180, 250, 255)};
  constexpr std::array labels{'X', 'Y', 'Z'};
  auto* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddCircleFilled(center, 3.0F, IM_COL32(205, 214, 244, 210));
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    ImVec2 end{center.x + (view[matrix_index(0U, axis)] * length),
               center.y - (view[matrix_index(1U, axis)] * length)};
    const auto projected = std::hypot(end.x - center.x, end.y - center.y);
    if (projected < 5.0F) {
      end = ImVec2{center.x + static_cast<float>(axis * 10U) - 10.0F, center.y + 12.0F};
      draw_list->AddCircle(center, 5.0F, colors[axis], 12, 2.0F);
    } else {
      draw_list->AddLine(center, end, colors[axis], 2.5F);
      draw_list->AddCircleFilled(end, 3.5F, colors[axis]);
    }
    const char label[2] = {labels[axis], '\0'};
    draw_list->AddText(ImVec2(end.x + 4.0F, end.y - 7.0F), colors[axis], label);
  }
}

struct launch_options {
  bool smoke = false;
  std::uint32_t smoke_frames = 3U;
  bool cooperative_tasks = false;
  std::string project;
};

bool parse_options(int argc, char** argv, launch_options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--smoke") {
      options.smoke = true;
    } else if (argument == "--smoke-frames" && index + 1 < argc) {
      const std::string_view count = argv[++index];
      const auto parsed =
          std::from_chars(count.data(), count.data() + count.size(), options.smoke_frames);
      if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() ||
          options.smoke_frames == 0U || options.smoke_frames > 100000U) {
        return false;
      }
      options.smoke = true;
    } else if (argument == "--cooperative-tasks") {
      options.cooperative_tasks = true;
    } else if (argument == "--project" && index + 1 < argc) {
      options.project = argv[++index];
    } else {
      return false;
    }
  }
  return true;
}

void report_startup_failure(std::string_view stage, gneiss::engine::error operation,
                            std::string_view path = {}) noexcept {
  const auto message = operation.message();
  std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=%.*s，结果=%d，消息=%.*s",
               static_cast<int>(stage.size()), stage.data(), operation.code(),
               static_cast<int>(message.size()), message.data());
  if (!path.empty()) {
    std::fprintf(stderr, "，路径=%.*s", static_cast<int>(path.size()), path.data());
  }
  std::fputc('\n', stderr);
}

void synchronize_history_dirty(editor_state& state) noexcept {
  if (state.editing.history().is_dirty()) {
    state.editing.document().mark_dirty();
  } else {
    state.editing.document().clear_dirty();
  }
}

gneiss::engine::error undo_editor_command(editor_state& state) noexcept {
  const auto result = state.editing.history().undo();
  if (result == gneiss::engine::error::success) {
    synchronize_history_dirty(state);
  }
  return result;
}

gneiss::engine::error redo_editor_command(editor_state& state) noexcept {
  const auto result = state.editing.history().redo();
  if (result == gneiss::engine::error::success) {
    synchronize_history_dirty(state);
  }
  return result;
}

[[nodiscard]] std::filesystem::path utf8_path(std::string_view value) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& value) {
  const auto text = value.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] gneiss::engine::error
read_author_asset(editor_state& state, std::string uri,
                  std::function<gneiss::engine::error(std::string)> next) {
  using namespace gneiss::engine;
  if (state.pending_file_operation)
    return error::not_ready;
  struct receipt {
    bool complete{};
    gneiss::engine::files::completion<gneiss::engine::files::bytes> value{error::not_ready};
  };
  auto received = std::make_shared<receipt>();
  gneiss::engine::files::request_id request;
  const auto status = state.application->async_files()->read(
      uri, {16U * 1024U * 1024U},
      [received](gneiss::engine::files::completion<gneiss::engine::files::bytes> value) {
        received->value = std::move(value);
        received->complete = true;
      },
      request);
  if (status.failed())
    return status;
  state.file_operation_saves_scene = false;
  state.pending_file_operation = [received, next = std::move(next)](error& value) {
    if (!received->complete)
      return false;
    value = received->value.status;
    if (value.ok())
      value = next(std::string(reinterpret_cast<const char*>(received->value.data.data()),
                               received->value.data.size()));
    return true;
  };
  return error::not_ready;
}

void restore_author_selection(editor_state& state, const author_selection& selection) noexcept {
  gneiss::engine::result<gneiss::editor::scene_node_record> scene_node{
      gneiss::engine::error::not_found};
  gneiss::engine::result<gneiss::editor::prefab_node_record> prefab_node{
      gneiss::engine::error::not_found};
  switch (selection.kind) {
  case author_selection_kind::scene_node:
    scene_node = state.editing.document().find_node(selection.primary_uuid);
    break;
  case author_selection_kind::prefab_root:
    prefab_node = state.editing.document().find_prefab_root(selection.primary_uuid);
    break;
  case author_selection_kind::prefab_source:
    prefab_node =
        state.editing.document().find_prefab_source(selection.primary_uuid, selection.source_uuid);
    break;
  case author_selection_kind::none:
    break;
  }
  if (bool(scene_node)) {
    (void)state.editing.document().select(scene_node->node);
  } else if (bool(prefab_node)) {
    (void)state.editing.document().select(prefab_node->node);
  }
}

[[nodiscard]] gneiss::engine::error
apply_author_snapshot(editor_state& state, const author_selection& selection,
                      const gneiss::editor::scene_document_snapshot& snapshot) noexcept {
  const auto status = gneiss::editor::restore_scene_document(*state.application,
                                                             state.editing.document(), snapshot);
  if (status.ok()) {
    state.editing.document().mark_dirty();
    restore_author_selection(state, selection);
  }
  return status;
}

[[nodiscard]] gneiss::engine::error
adopt_author_documents(editor_state& state, std::string label,
                       std::shared_ptr<gneiss::editor::scene_document_snapshot> before,
                       std::shared_ptr<gneiss::editor::scene_document_snapshot> after,
                       const author_selection& undo_selection,
                       const author_selection& redo_selection, bool history_boundary) {
  if (history_boundary) {
    // 共享来源已经保存；旧快照不能再通过普通撤销恢复旧来源投影。
    (void)state.editing.history().clear(false);
    state.editing.document().mark_dirty();
  }
  auto status = apply_author_snapshot(state, redo_selection, *after);
  if (status.failed() || history_boundary)
    return status;
  status = state.editing.history().record(
      {.label = std::move(label),
       .undo = [&state, undo_selection,
                before] { return apply_author_snapshot(state, undo_selection, *before); },
       .redo = [&state, redo_selection,
                after] { return apply_author_snapshot(state, redo_selection, *after); },
       .merge_key = {}});
  if (status.failed()) {
    (void)apply_author_snapshot(state, undo_selection, *before);
    synchronize_history_dirty(state);
  }
  return status;
}

[[nodiscard]] gneiss::engine::error
record_author_documents(editor_state& state, std::string label,
                        std::vector<gneiss::editor::author_document_change> changes,
                        const author_selection& undo_selection,
                        const author_selection& redo_selection, bool save_source = false,
                        bool history_boundary = false, bool load_missing = false) {
  if (state.editing.history().is_pending())
    return gneiss::engine::error::not_ready;
  if (load_missing) {
    if (save_source || state.pending_file_operation)
      return gneiss::engine::error::invalid_state;
    auto operation = std::make_shared<gneiss::editor::scene_document_edit>();
    auto status =
        operation->begin(*state.application, state.editing.document(), changes, std::move(label),
                         [&state, undo_selection, redo_selection](bool undo) {
                           restore_author_selection(state, undo ? undo_selection : redo_selection);
                         });
    if (status.failed())
      return status;
    state.file_operation_saves_scene = false;
    state.pending_file_operation = [&state, operation](gneiss::engine::error& value) {
      value =
          operation->advance(*state.application, state.editing.document(), state.editing.history());
      if (value.ok())
        state.prefab_author_diagnostic = {};
      return value != gneiss::engine::error::not_ready;
    };
    return gneiss::engine::error::not_ready;
  }
  auto captured =
      gneiss::editor::capture_scene_document(*state.application, state.editing.document());
  if (!captured)
    return captured.error();
  auto prepared = gneiss::editor::prepare_scene_document(*captured, changes);
  if (!prepared)
    return prepared.error();
  if (!prepared->missing.empty())
    return gneiss::engine::error::not_ready;
  auto before = std::make_shared<gneiss::editor::scene_document_snapshot>(std::move(*captured));
  auto after =
      std::make_shared<gneiss::editor::scene_document_snapshot>(std::move(prepared->snapshot));
  auto status = gneiss::engine::error{gneiss::engine::error::success};
  if (save_source) {
    // 规划器只允许一个来源；父场景变更仅用于准备内存快照。
    const gneiss::editor::author_document_change* source = nullptr;
    for (const auto& change : changes) {
      if ("asset://" + change.path == before->uri)
        continue;
      if (source || !change.path.ends_with(".prefab.json"))
        return gneiss::engine::error::invalid_argument;
      source = &change;
    }
    if (!source)
      return gneiss::engine::error::invalid_argument;
    std::shared_ptr<gneiss::editor::author_file_save> receipt;
    status =
        gneiss::editor::begin_author_file_save(*state.application->async_files(), *source, receipt);
    if (status.failed())
      return status;
    const auto source_uri = "asset://" + source->path;
    const auto content = *source->replacement;
    state.file_operation_saves_scene = false;
    state.pending_file_operation = [&state, receipt, source_uri, content, label = std::move(label),
                                    before, after, undo_selection, redo_selection,
                                    history_boundary](gneiss::engine::error& value) mutable {
      if (!receipt->complete)
        return false;
      value = receipt->status;
      if (value.failed())
        return true;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
      (void)state.author_assets.acknowledge(source_uri, content);
      (void)state.runtime.publish_asset_revision(std::span<const std::string>(&source_uri, 1U));
#endif
      value = adopt_author_documents(state, std::move(label), before, after, undo_selection,
                                     redo_selection, history_boundary);
      return true;
    };
    return gneiss::engine::error::not_ready;
  }
  return adopt_author_documents(state, std::move(label), before, after, undo_selection,
                                redo_selection, history_boundary);
}

[[nodiscard]] gneiss::engine::error current_author_scene(editor_state& state,
                                                         author_scene_document& output) noexcept {
  const auto& uri = state.editing.document().uri();
  if (!std::string_view(uri).starts_with("asset://"))
    return gneiss::engine::error::invalid_state;
  output.relative_path = std::string_view(uri).substr(8);
  auto scene_result =
      state.application->scenes()->serialize(state.editing.document().scene_handle());
  if (!scene_result)
    return scene_result.error();
  output.content = std::move(*scene_result);
  return gneiss::engine::error::success;
}

gneiss::engine::error restore_prefab_component(editor_state& state, gneiss::engine::guid instance,
                                               gneiss::engine::guid source,
                                               gneiss::engine::guid component) {
  author_scene_document scene;
  auto status = current_author_scene(state, scene);
  if (status.failed())
    return status;
  auto changes = gneiss::editor::prepare_restore_prefab_component(
      scene.content, scene.relative_path, instance, source, component);
  if (!changes)
    return changes.error();
  const author_selection selection{.kind = author_selection_kind::prefab_source,
                                   .primary_uuid = instance,
                                   .source_uuid = source};
  return record_author_documents(state, "恢复来源组件", std::move(*changes), selection, selection,
                                 false, false, true);
}

void draw_prefab_overrides(editor_state& state) {
  using namespace gneiss::engine;
  using namespace gneiss::editor;
  const auto selected = state.editing.document().selected_prefab_node();
  if (!selected)
    return;
  const auto instance = selected->instance_uuid;
  const auto selected_source = selected->source_node_uuid;
  auto captured = state.application->scenes()->capture_prefab_header(
      state.editing.document().scene_handle(), instance);
  if (!captured || !captured->author.prefab || !captured->source)
    return;
  const auto& author = captured->author;
  const auto& source = captured->source;
  auto entries = describe_prefab_overrides(*author.prefab, *source);
  if (!entries) {
    state.inspector_error = entries.error();
    return;
  }
  if (!ImGui::CollapsingHeader("Instance Overrides", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  std::optional<prefab_override_entry> restore;
  ImGui::BeginDisabled(entries->empty() || state.runtime.is_busy() ||
                       state.editing.history().is_pending() || state.editing.assets().active() ||
                       static_cast<bool>(state.pending_file_operation));
  const bool restore_all = ImGui::Button("Restore All Overrides");
  int index = 0;
  for (const auto& entry : *entries) {
    ImGui::PushID(index++);
    ImGui::TextWrapped("%s: %s", entry.component ? (entry.added ? "Added" : "Removed") : "Modified",
                       entry.label.c_str());
    const auto node =
        state.editing.document().find_prefab_source(instance, entry.key.source_node_uuid);
    ImGui::BeginDisabled(!node);
    if (ImGui::SmallButton("Locate") && node)
      (void)state.editing.document().select(node->node);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Restore"))
      restore = entry;
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  if (restore_all || restore) {
    author_scene_document scene;
    auto status = current_author_scene(state, scene);
    if (status.ok()) {
      auto changes = prepare_restore_prefab(scene.content, scene.relative_path, instance,
                                            restore_all ? nullptr : &*restore);
      const author_selection selection{.kind = author_selection_kind::prefab_source,
                                       .primary_uuid = instance,
                                       .source_uuid = selected_source};
      status = changes ? record_author_documents(state, "恢复 Prefab 覆盖", std::move(*changes),
                                                 selection, selection)
                       : changes.error();
    }
    state.history_error = status;
    if (status.ok())
      state.prefab_author_diagnostic = {};
  }
}

void draw_prefab_conflict(editor_state& state) {
  using namespace gneiss::engine;
  const auto& diagnostic = state.prefab_author_diagnostic;
  if (diagnostic.path.empty())
    return;
  ImGui::TextWrapped("%s", diagnostic.message.c_str());
  ImGui::TextWrapped("%s", diagnostic.path.c_str());
  const auto read_id = [&](std::string_view marker) {
    const auto offset = diagnostic.path.find(marker);
    return offset == std::string::npos
               ? std::optional<guid>{}
               : guid::parse(std::string_view(diagnostic.path).substr(offset + marker.size(), 36));
  };
  const auto instance = read_id("/instances/");
  const auto object = read_id("/objects/");
  const auto type = read_id("/components/");
  if (!instance || !object || !type)
    return;
  auto captured = state.application->scenes()->capture_prefab_header(
      state.editing.document().scene_handle(), *instance);
  if (captured && captured->source && captured->author.prefab) {
    const auto& author = captured->author;
    const auto& source = captured->source;
    ImGui::Text("Instance: %s", author.name.c_str());
    auto entries = gneiss::editor::describe_prefab_overrides(*author.prefab, *source);
    if (entries)
      for (const auto& entry : *entries) {
        std::string path = "/instances/" + instance->string() + "/objects/" +
                           entry.key.source_node_uuid.string() + "/components/" +
                           entry.key.type_guid.string();
        if (!entry.component) {
          path += "/properties/" + std::to_string(entry.key.property_id.get());
          for (const auto& step : entry.key.path)
            path += std::holds_alternative<meta::property_id>(step)
                        ? "/properties/" + std::to_string(std::get<meta::property_id>(step).get())
                        : "/indices/" + std::to_string(std::get<std::size_t>(step));
        }
        if (path == diagnostic.path) {
          ImGui::TextWrapped("%s", entry.label.c_str());
          break;
        }
      }
  }
  auto node = state.editing.document().find_prefab_source(*instance, *object);
  if (!node)
    node = state.editing.document().find_prefab_root(*instance);
  ImGui::BeginDisabled(!node);
  if (ImGui::Button("Locate Prefab Conflict") && node)
    (void)state.editing.document().select(node->node);
  ImGui::EndDisabled();
}

bool has_prefab_overrides(editor_state& state, gneiss::engine::guid instance) {
  auto captured = state.application->scenes()->capture_prefab_header(
      state.editing.document().scene_handle(), instance);
  if (!captured || !captured->author.prefab)
    return false;
  const auto& prefab = *captured->author.prefab;
  return !prefab.overrides.empty() || !prefab.component_overrides.empty();
}

[[nodiscard]] gneiss::engine::error
apply_selected_prefab(editor_state& state, gneiss::engine::guid instance_uuid,
                      gneiss::engine::guid source_uuid,
                      std::optional<std::string> source_content = {}) {
  const auto selected = state.editing.document().find_prefab_source(instance_uuid, source_uuid);
  if (!selected || !has_prefab_overrides(state, instance_uuid) || state.runtime.is_busy()) {
    return gneiss::engine::error::invalid_state;
  }
  if (!source_content) {
    return read_author_asset(
        state, selected->prefab_uri,
        [&state, instance = instance_uuid, source = source_uuid](std::string content) {
          return apply_selected_prefab(state, instance, source, std::move(content));
        });
  }
  author_scene_document scene;
  auto operation = current_author_scene(state, scene);
  author_asset_document prefab{std::string(selected->prefab_uri.substr(8)),
                               std::move(*source_content)};
  gneiss::editor::apply_prefab_author_plan plan;
  if (operation == gneiss::engine::error::success) {
    operation = gneiss::editor::prepare_apply_prefab(scene.content, prefab.content,
                                                     {.scene_path = scene.relative_path,
                                                      .prefab_path = prefab.relative_path,
                                                      .prefab_uri = selected->prefab_uri,
                                                      .instance_uuid = instance_uuid},
                                                     plan, &state.prefab_author_diagnostic);
  }
  if (operation == gneiss::engine::error::success) {
    const author_selection selection{.kind = author_selection_kind::prefab_source,
                                     .primary_uuid = instance_uuid,
                                     .source_uuid = source_uuid};
    operation = record_author_documents(state, "应用 Prefab 实例覆盖", std::move(plan.changes),
                                        selection, selection, true, true);
  }
  return operation;
}

[[nodiscard]] gneiss::engine::error
unpack_selected_prefab(editor_state& state, gneiss::engine::guid instance_uuid,
                       std::optional<std::string> source_content = {}) {
  const auto root = state.editing.document().find_prefab_root(instance_uuid);
  if (!root || state.runtime.is_busy()) {
    return gneiss::engine::error::invalid_state;
  }
  auto query = state.editing.document().capture_nodes();
  if (!query)
    return query.error();
  const auto prefab_uri = root->prefab_uri;
  if (!source_content) {
    return read_author_asset(state, prefab_uri,
                             [&state, instance = instance_uuid](std::string content) {
                               return unpack_selected_prefab(state, instance, std::move(content));
                             });
  }
  author_scene_document scene;
  auto operation = current_author_scene(state, scene);
  author_asset_document prefab{prefab_uri.substr(8), std::move(*source_content)};
  gneiss::engine::guid unpacked_root_uuid;
  std::vector<gneiss::engine::guid> target_uuids;
  std::vector<gneiss::editor::unpack_prefab_uuid_mapping> mappings;
  if (operation == gneiss::engine::error::success) {
    const auto count = std::ranges::count_if(
        query->prefab_nodes, [&](const auto& node) { return node.instance_uuid == instance_uuid; });
    target_uuids.reserve(static_cast<std::size_t>(count));
    mappings.reserve(static_cast<std::size_t>(count));
    for (const auto& node : query->prefab_nodes) {
      if (node.instance_uuid == instance_uuid) {
        target_uuids.emplace_back();
        operation = gneiss::editor::make_editor_uuid(target_uuids.back());
        if (operation != gneiss::engine::error::success) {
          break;
        }
        if (node.is_instance_root)
          unpacked_root_uuid = target_uuids.back();
        mappings.push_back(
            {.source_node_uuid = node.source_node_uuid, .target_node_uuid = target_uuids.back()});
      }
    }
  }
  std::vector<gneiss::editor::author_document_change> changes;
  if (operation == gneiss::engine::error::success) {
    operation = gneiss::editor::prepare_unpack_prefab(scene.content, prefab.content,
                                                      {.scene_path = scene.relative_path,
                                                       .prefab_uri = prefab_uri,
                                                       .instance_uuid = instance_uuid,
                                                       .node_mappings = mappings},
                                                      changes, &state.prefab_author_diagnostic);
  }
  if (operation == gneiss::engine::error::success) {
    operation = record_author_documents(state, "解包 Prefab 实例", std::move(changes),
                                        {.kind = author_selection_kind::prefab_root,
                                         .primary_uuid = instance_uuid,
                                         .source_uuid = {}},
                                        {.kind = author_selection_kind::scene_node,
                                         .primary_uuid = unpacked_root_uuid,
                                         .source_uuid = {}});
  }
  return operation;
}

[[nodiscard]] gneiss::engine::error create_prefab_from_selected(editor_state& state,
                                                                gneiss::engine::guid root_uuid,
                                                                std::string_view prefab_path) {
  if (state.editing.document().find_node(root_uuid).failed() || state.runtime.is_busy() ||
      prefab_path.empty()) {
    return gneiss::engine::error::invalid_state;
  }
  author_scene_document scene;
  auto operation = current_author_scene(state, scene);
  std::string prefab_uri = "asset://";
  prefab_uri.append(prefab_path);
  gneiss::engine::guid prefab_uuid;
  gneiss::engine::guid instance_uuid;
  if (operation == gneiss::engine::error::success) {
    operation = gneiss::editor::make_editor_uuid(prefab_uuid);
  }
  if (operation == gneiss::engine::error::success) {
    operation = gneiss::editor::make_editor_uuid(instance_uuid);
  }
  std::vector<gneiss::editor::author_document_change> changes;
  if (operation == gneiss::engine::error::success) {
    operation = gneiss::editor::prepare_create_prefab(scene.content,
                                                      {.scene_path = scene.relative_path,
                                                       .prefab_path = prefab_path,
                                                       .prefab_uri = prefab_uri,
                                                       .root_uuid = root_uuid,
                                                       .prefab_uuid = prefab_uuid,
                                                       .instance_uuid = instance_uuid},
                                                      changes);
  }
  if (operation == gneiss::engine::error::success) {
    operation = record_author_documents(
        state, "从场景子树创建 Prefab", std::move(changes),
        {.kind = author_selection_kind::scene_node, .primary_uuid = root_uuid, .source_uuid = {}},
        {.kind = author_selection_kind::prefab_root,
         .primary_uuid = instance_uuid,
         .source_uuid = {}},
        true);
  }
  return operation;
}

gneiss::engine::error begin_scene_save(editor_state& state, std::string_view uri,
                                       gneiss::engine::files::write_mode mode) {
  if (state.pending_file_operation)
    return gneiss::engine::error::not_ready;
  std::shared_ptr<gneiss::editor::scene_document::save_receipt> receipt;
  const auto status =
      state.editing.document().begin_save(*state.application->async_files(), uri, mode, receipt);
  if (status.failed())
    return status;
  state.file_operation_saves_scene = true;
  state.pending_file_operation = [&state, receipt](gneiss::engine::error& value) {
    if (!receipt->complete)
      return false;
    value = state.editing.document().accept_save(*receipt);
    if (value.ok()) {
      if (!state.editing.document().is_dirty())
        (void)state.editing.history().mark_saved();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
      (void)state.author_assets.acknowledge(receipt->uri, receipt->content);
      (void)state.runtime.publish_asset_revision(std::span<const std::string>(&receipt->uri, 1U));
#endif
    }
    return true;
  };
  return gneiss::engine::error::not_ready;
}
gneiss::engine::error save_document_as(editor_state& state) {
  if (state.pending_file_operation)
    return gneiss::engine::error::not_ready;
  std::filesystem::path path;
  auto status = gneiss::host::select_scene_save_path(path);
  std::string uri;
  if (status.ok())
    status = gneiss::editor::make_asset_uri(state.asset_root, path, uri);
  return status.failed()
             ? status
             : begin_scene_save(state, uri, gneiss::engine::files::write_mode::create_new);
}
gneiss::engine::error save_document(editor_state& state) {
  if (state.editing.document().uri().empty())
    return save_document_as(state);
  return begin_scene_save(state, state.editing.document().uri(),
                          gneiss::engine::files::write_mode::replace);
}

gneiss::engine::error launch_runtime(editor_state& state, bool save_changes) noexcept {
#if defined(GNEISS_EDITOR_HAS_RUNTIME)
  if (save_changes) {
    const auto status = save_document(state);
    state.run_after_save = bool(state.pending_file_operation);
    return status;
  }
  gneiss::editor::runtime_launch_request request;
  auto operation = gneiss::engine::error::success;
  if (operation == gneiss::engine::error::success &&
      gneiss::editor::inspect_runtime_launch(state.editing.document(), state.project_root,
                                             request) !=
          gneiss::editor::runtime_launch_state::ready) {
    operation = gneiss::engine::error::invalid_state;
  }
  if (operation == gneiss::engine::error::success) {
    operation = state.runtime.start(std::filesystem::path{GNEISS_EDITOR_RUNTIME_PATH}, request);
  }
  return operation;
#else
  (void)state;
  (void)save_changes;
  return gneiss::engine::error::unsupported;
#endif
}

void request_runtime_launch(editor_state& state) noexcept {
  gneiss::editor::runtime_launch_request request;
  const auto launch_state =
      gneiss::editor::inspect_runtime_launch(state.editing.document(), state.project_root, request);
  if (launch_state == gneiss::editor::runtime_launch_state::requires_save) {
    state.pending_save_and_run = true;
    ImGui::OpenPopup("Save and Run");
    return;
  }
  state.runtime_result = launch_runtime(state, false);
  state.runtime_attempted = true;
}

void begin_editor_exit(editor_state& state) {
  state.material_editor.advance();
  if (state.material_editor.saving() || state.material_editor.is_dirty()) {
    state.material_editor_visible = true;
    state.material_edit_status = gneiss::engine::error::invalid_state;
    return;
  }
  if (state.closing)
    return;
  state.closing = true;
  if (state.runtime.is_running())
    state.runtime_result = state.runtime.request_stop();
  state.editing.cancel(*state.application);
  if (state.scene_loader.active())
    (void)state.scene_loader.cancel(*state.application);
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  state.asset_reimports.request_stop();
  state.author_assets.request_stop();
#endif
}

gneiss::engine::error perform_document_action(editor_state& state,
                                              gneiss::editor::document_action action) {
  gneiss::engine::error operation = gneiss::engine::error::success;
  switch (action) {
  case gneiss::editor::document_action::new_scene:
    operation = state.editing.create_empty(*state.application, state.world);
    break;
  case gneiss::editor::document_action::open_scene: {
    std::filesystem::path path;
    operation = gneiss::host::select_scene_file(path);
    std::string uri;
    if (operation == gneiss::engine::error::success) {
      operation = gneiss::editor::make_asset_uri(state.asset_root, path, uri);
    }
    if (operation == gneiss::engine::error::success) {
      operation = state.scene_loader.request(*state.application, state.editing, uri);
    }
    break;
  }
  case gneiss::editor::document_action::exit_editor:
    begin_editor_exit(state);
    break;
  case gneiss::editor::document_action::none:
    return gneiss::engine::error::success;
  }
  if (operation == gneiss::engine::error::success &&
      action == gneiss::editor::document_action::new_scene) {
    state.editing.history().clear();
  }
  return operation;
}

void request_document_action(editor_state& state, gneiss::editor::document_action action) {
  if (state.pending_file_operation)
    return;
  const auto ready = state.document_actions.request(action, state.editing.document().is_dirty());
  if (ready != gneiss::editor::document_action::none) {
    state.history_error = perform_document_action(state, ready);
  }
}

bool reparent_with_history(editor_state& state, gneiss::engine::guid source_uuid,
                           gneiss::engine::guid target_uuid) {
  const auto source = state.editing.document().find_node(source_uuid);
  const auto target = target_uuid.is_nil() ? gneiss::engine::error{gneiss::engine::error::not_found}
                                           : state.editing.document().find_node(target_uuid);
  if (!source || (!target_uuid.is_nil() && !target) ||
      (bool(target) && source->node == target->node)) {
    return false;
  }
  gneiss::engine::guid previous_parent_uuid;
  if (source->parent.is_valid()) {
    auto query = state.editing.document().capture_nodes();
    if (!query)
      return false;
    const auto parent =
        std::ranges::find(query->nodes, source->parent, &gneiss::editor::scene_node_record::node);
    if (parent != query->nodes.end()) {
      previous_parent_uuid = parent->uuid;
    }
  }
  if (previous_parent_uuid == target_uuid) {
    return false;
  }
  const gneiss::engine::guid source_key{source_uuid};
  const gneiss::engine::guid target_key{target_uuid};
  state.history_error = state.editing.document().reparent_node(
      source->node, !target ? gneiss::scene_node_id{} : target->node);
  if (state.history_error != gneiss::engine::error::success) {
    return false;
  }
  state.history_error = state.editing.history().record(
      {.label = "移动节点",
       .undo =
           [&state, source_key, previous_parent_uuid] {
             const auto current = state.editing.document().find_node(source_key);
             const auto parent = previous_parent_uuid.is_nil()
                                     ? gneiss::engine::error{gneiss::engine::error::not_found}
                                     : state.editing.document().find_node(previous_parent_uuid);
             return !current ? gneiss::engine::error::not_found
                             : state.editing.document().reparent_node(
                                   current->node, !parent ? gneiss::scene_node_id{} : parent->node);
           },
       .redo =
           [&state, source_key, target_key] {
             const auto current = state.editing.document().find_node(source_key);
             const auto parent = target_key.is_nil()
                                     ? gneiss::engine::error{gneiss::engine::error::not_found}
                                     : state.editing.document().find_node(target_key);
             return !current || (!target_key.is_nil() && !parent)
                        ? gneiss::engine::error::not_found
                        : state.editing.document().reparent_node(
                              current->node, !parent ? gneiss::scene_node_id{} : parent->node);
           },
       .merge_key = {}});
  if (state.history_error != gneiss::engine::error::success) {
    const auto current = state.editing.document().find_node(source_key);
    const auto parent = previous_parent_uuid.is_nil()
                            ? gneiss::engine::error{gneiss::engine::error::not_found}
                            : state.editing.document().find_node(previous_parent_uuid);
    if (bool(current)) {
      (void)state.editing.document().reparent_node(current->node, !parent ? gneiss::scene_node_id{}
                                                                          : parent->node);
    }
  }
  return true;
}

gneiss::editor::runtime_inspector_actions
make_runtime_inspector_actions(editor_state& state,
                               const gneiss::editor::runtime_scene_node& node) {
  return {.context = &state,
          .editable = state.runtime.supports_property_editing(),
          .can_apply_to_author =
              !node.uuid.is_nil() && state.editing.document().find_node(node.uuid).ok(),
          .find_edit =
              [](void* context, const gneiss::editor::runtime_property_key& key) {
                return static_cast<editor_state*>(context)->runtime.property_edit(key);
              },
          .write_property =
              [](void* context, const gneiss::editor::runtime_property_key& key,
                 std::uint64_t revision, gneiss::editor::runtime_property_value value) {
                return static_cast<editor_state*>(context)->runtime.request_property_write(
                    key, revision, {std::move(value.payload)});
              },
          .apply_to_author =
              [](void* context, const gneiss::editor::runtime_scene_node& selected) {
                auto& editor = *static_cast<editor_state*>(context);
                editor.history_error = gneiss::editor::apply_runtime_transform_to_author(
                    editor.editing.document(), editor.editing.history(),
                    {selected.uuid, selected.prefab_instance_uuid, selected.prefab_source_node_uuid,
                     selected.local_transform});
                if (editor.history_error == gneiss::engine::error::success) {
                  synchronize_history_dirty(editor);
                }
              },
          .report_result =
              [](void* context, gneiss::engine::error status) {
                auto& editor = *static_cast<editor_state*>(context);
                editor.runtime_result = status;
                editor.runtime_attempted = true;
              }};
}

void draw_component_descriptions(editor_state& state) {
  using namespace gneiss;
  using namespace gneiss::engine;
  using namespace gneiss::editor;
  component_edit_target identity;
  scene_node_id node_id;
  if (const auto node = state.editing.document().selected_prefab_node()) {
    identity.instance_uuid = node->instance_uuid;
    identity.source_uuid = node->source_node_uuid;
    node_id = node->node;
  } else if (const auto selected = state.editing.document().selected_node()) {
    identity.object_uuid = selected->uuid;
    node_id = selected->node;
  } else
    return;
  prefab_asset_ref source_asset;
  const object_description* source_object = nullptr;
  bool source_transform = false;
  if (identity.instance_uuid) {
    object_description author;
    auto scenes_result = state.application->scenes()->capture_prefab_header(
        state.editing.document().scene_handle(), identity.instance_uuid);
    const auto captured = scenes_result ? gneiss::engine::error::success : scenes_result.error();
    if (scenes_result) {
      author = std::move(scenes_result->author);
      source_asset = std::move(scenes_result->source);
    }
    if (captured.failed()) {
      state.inspector_error = captured;
      return;
    }
    const auto found =
        std::ranges::find(source_asset->objects, identity.source_uuid, &object_description::uuid);
    if (found != source_asset->objects.end()) {
      source_object = &*found;
      source_transform = identity.source_uuid != source_asset->root_uuid;
    }
  }
  const auto source_view = [&](const auto* value) -> std::optional<meta::instance> {
    if (!value)
      return {};
    auto view = meta::instance::from(*value);
    if (!view) {
      state.inspector_error = view.error();
      return {};
    }
    return *view;
  };
  auto transform_source = source_view(source_transform ? &source_object->transform : nullptr);
  identity.component_type = transform_component_type;
  math::transform transform;
  auto transform_status = state.world->node_get_local_transform(node_id, transform);
  if (transform_status.failed()) {
    state.inspector_error = transform_status;
    return;
  }
  auto transform_view = meta::instance::from(transform);
  if (!transform_view) {
    state.inspector_error = transform_view.error();
    return;
  }
  auto transform_edit = gneiss::editor::draw_reflected_properties(
      *transform_view, "Transform", state.property_edit_serial, state.inspector_error, {},
      transform_source ? &*transform_source : nullptr);
  if (transform_edit)
    state.inspector_error =
        edit_component_description(state.editing.document(), state.editing.history(), *state.world,
                                   identity, *transform_edit, state.property_edit_serial);
  if (state.inspector_error.failed())
    ImGui::TextWrapped("%s", state.inspector_error.message().data());
  auto components = state.editing.document().components(node_id);
  if (!components) {
    state.inspector_error = components.error();
    return;
  }
  if (identity.instance_uuid) {
    auto types = component_editor_types();
    ImGui::BeginDisabled(state.editing.assets().active() || state.editing.history().is_pending());
    if (ImGui::Button("Components"))
      ImGui::OpenPopup("prefab_components");
    if (ImGui::BeginPopup("prefab_components")) {
      if (types)
        for (const auto& type : *types) {
          auto metadata = meta::describe_type(type.type);
          auto change = state.editing.document().capture_component_override(node_id, type.type);
          if (!metadata || !change)
            continue;
          const bool exists = components->find(type.type) != nullptr;
          const bool from_source = source_object && source_object->components.find(type.type);
          identity.component_type = type.type;
          ImGui::PushID(type.type.string().c_str());
          const std::string name(metadata->name);
          bool acted = false;
          auto invoke = [&](prefab_component_action action) {
            state.history_error =
                action == prefab_component_action::restore_source
                    ? restore_prefab_component(state, identity.instance_uuid, identity.source_uuid,
                                               identity.component_type)
                    : edit_prefab_component_structure(state.editing.document(),
                                                      state.editing.history(), identity, action);
            acted = true;
          };
          if (exists && type.removable && ImGui::MenuItem(("Remove " + name).c_str()))
            invoke(prefab_component_action::remove);
          if (!exists && !from_source && type.addable && ImGui::MenuItem(("Add " + name).c_str()))
            invoke(prefab_component_action::add);
          if ((change->removed || change->added || !change->properties.empty()) &&
              ImGui::MenuItem(("Restore Source: " + name).c_str()))
            invoke(prefab_component_action::restore_source);
          ImGui::PopID();
          if (acted)
            break;
        }
      ImGui::EndPopup();
    }
    ImGui::EndDisabled();
  }
  for (const auto& component : *components) {
    identity.component_type = component.guid();
    auto view = component.view();
    auto type = meta::describe_type(component.guid());
    if (!view || !type) {
      state.inspector_error = !view ? view.error() : type.error();
      continue;
    }
    std::optional<meta::instance> original;
    if (source_object) {
      if (const auto* source = source_object->components.find(component.guid())) {
        auto original_view = source->view();
        if (original_view)
          original = *original_view;
        else
          state.inspector_error = original_view.error();
      }
    }
    auto editor = component_editor(component.guid());
    if (!editor) {
      state.inspector_error = editor.error();
      continue;
    }
    const bool asset = editor->route == component_editor_route::mesh_assets;
    ImGui::PushID(component.guid().string().c_str());
    ImGui::BeginDisabled(state.editing.assets().active() || state.editing.history().is_pending());
    auto edit = gneiss::editor::draw_reflected_properties(
        *view, std::string(type->name).c_str(), state.property_edit_serial, state.inspector_error,
        {}, original ? &*original : nullptr);
    if (edit) {
      state.inspector_error =
          asset ? state.editing.assets().request_property(*state.application,
                                                          state.editing.document(),
                                                          state.editing.history(), identity, *edit)
                : edit_component_description(state.editing.document(), state.editing.history(),
                                             *state.world, identity, *edit,
                                             state.property_edit_serial);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    if (state.inspector_error.failed())
      ImGui::TextWrapped("%s", state.inspector_error.message().data());
  }
}

#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
void execute_asset_browser_request(editor_state& state,
                                   const gneiss::editor::asset_browser_request& request) {
  using namespace gneiss::host;
  using action = gneiss::editor::asset_browser_action;
  const auto entry = std::ranges::find(state.assets.entries(), request.asset_id,
                                       &gneiss::editor::asset_browser_entry::id);
  const auto queued = [&state](bool accepted) {
    if (!accepted) {
      state.last_import = {};
      state.last_import.diagnostic = "导入队列已满或正在关闭，请重试";
      state.import_attempted = true;
    }
  };
  switch (request.action) {
  case action::edit_material:
    if (entry != state.assets.entries().end() && is_material_asset(*entry) &&
        entry->kind == gneiss::editor::asset_browser_kind::authored_asset &&
        !state.material_editor.is_dirty() && !state.material_editor.saving() &&
        !state.pending_file_operation) {
      state.material_editor_visible = true;
      const auto uri = entry->asset_uri;
      state.material_edit_status =
          read_author_asset(state, uri, [&state, uri](std::string content) {
            state.material_edit_status = state.material_editor.open(uri, std::move(content));
            return state.material_edit_status;
          });
      if (state.pending_file_operation) {
        auto operation = std::move(state.pending_file_operation);
        state.pending_file_operation = [&state, operation = std::move(operation)](auto& status) {
          const bool complete = operation(status);
          if (complete)
            state.material_edit_status = status;
          return complete;
        };
      }
    }
    break;
  case action::none:
    break;
  case action::restart_source_watch: {
    const auto stopped = state.asset_watcher.is_running() ? state.asset_watcher.stop()
                                                          : gneiss::engine::error::success;
    state.asset_watch_result =
        stopped.ok() ? state.asset_watcher.start(state.project_root / "sources", true) : stopped;
    if (state.asset_watch_result.ok()) {
      state.asset_reimports.request_rescan();
    }
    state.asset_watch_failed = state.asset_watch_result != gneiss::engine::error::success &&
                               state.asset_watch_result != gneiss::engine::error::not_ready;
    break;
  }
  case action::restart_author_watch: {
    const auto stopped = state.author_asset_watcher.is_running() ? state.author_asset_watcher.stop()
                                                                 : gneiss::engine::error::success;
    state.author_watch_result =
        stopped.ok() ? state.author_asset_watcher.start(state.asset_root, false) : stopped;
    if (state.author_watch_result.ok()) {
      state.author_assets.request_rescan();
    }
    break;
  }
  case action::scan_sources:
    state.asset_reimports.request_rescan();
    break;
  case action::scan_author_assets:
    state.author_assets.request_rescan();
    break;
  case action::refresh:
    state.asset_reimports.request_refresh();
    break;
  case action::import_asset: {
    std::filesystem::path selected;
    const auto selected_result = select_source_asset(selected);
    if (selected_result.ok()) {
      queued(state.asset_reimports.import_asset(selected, true));
    } else if (selected_result != gneiss::engine::error::not_ready) {
      state.last_import = {};
      state.last_import.result = gneiss::editor::editor_import_result::io_error;
      state.last_import.diagnostic = std::string{selected_result.message()};
      state.import_attempted = true;
    }
    break;
  }
  case action::reimport_selected:
    if (entry != state.assets.entries().end() &&
        entry->kind == gneiss::editor::asset_browser_kind::source) {
      queued(state.asset_reimports.import_asset(state.project_root / "sources" /
                                                utf8_path(entry->relative_path)));
    }
    break;
  case action::cancel_tasks:
    state.asset_reimports.cancel();
    break;
  case action::retry_import:
    (void)state.asset_reimports.import_asset(state.last_import.source_path);
    break;
  case action::cancel_scene_load:
    state.runtime_result = state.runtime.cancel_scene_load();
    break;
  case action::retry_scene_load:
    state.runtime_result = state.runtime.retry_scene_load();
    break;
  case action::load_scene:
    if (entry != state.assets.entries().end()) {
      state.runtime_result = state.runtime.load_scene(entry->asset_uri);
    }
    break;
  case action::cancel_runtime_reload:
    state.runtime_result = state.runtime.cancel_asset_reload();
    break;
  case action::retry_runtime_reload:
    state.runtime_result = state.runtime.retry_asset_reload();
    break;
  case action::add_mesh:
    if (entry != state.assets.entries().end() && is_mesh_asset(*entry)) {
      const auto* material = find_material_for_mesh(state.assets.entries(), *entry);
      if (bool(material)) {
        state.asset_scene_result = state.editing.assets().request_add(
            *state.application, state.editing.document(), state.editing.history(),
            entry->display_name,
            {.mesh_uri = entry->asset_uri, .material_uri = material->asset_uri});
        state.asset_scene_attempted = true;
      }
    }
    break;
  case action::add_prefab:
    if (entry != state.assets.entries().end() && is_prefab_asset(*entry)) {
      const auto node = state.editing.document().selected_node();
      state.asset_scene_result = state.editing.prefabs().request(
          *state.application, state.task_scheduler, state.editing.document(),
          state.editing.history(), entry->display_name, entry->asset_uri,
          !node ? gneiss::scene_node_id{} : node->node);
      state.asset_scene_attempted = true;
    }
    break;
  case action::apply_to_node: {
    gneiss::editor::component_edit_target target;
    target.component_type = gneiss::engine::mesh_renderer_description::type_guid;
    if (const auto node = state.editing.document().selected_prefab_node()) {
      target.instance_uuid = node->instance_uuid;
      target.source_uuid = node->source_node_uuid;
    } else if (const auto selected = state.editing.document().selected_node())
      target.object_uuid = selected->uuid;
    if ((target.object_uuid || target.instance_uuid) && entry != state.assets.entries().end() &&
        (is_mesh_asset(*entry) || is_material_asset(*entry))) {
      gneiss::editor::property_edit edit;
      edit.path.emplace_back(gneiss::engine::meta::property_id{is_mesh_asset(*entry) ? 1U : 2U});
      edit.data = gneiss::engine::value{entry->asset_uri};
      state.asset_scene_result = state.editing.assets().request_property(
          *state.application, state.editing.document(), state.editing.history(), target, edit);
      state.asset_scene_attempted = true;
    }
    break;
  }
  }
}

void draw_asset_browser(editor_state& state) {
  using namespace gneiss::host;
  const auto background = state.asset_reimports.status();
  const auto source_root = path_utf8(state.project_root / "sources");
  const auto author_root = path_utf8(state.asset_root);
  const auto worker_source = path_utf8(background.source);
  gneiss::editor::asset_browser_view view;
  view.watches[0] = {.root = source_root,
                     .operation = state.asset_watch_result,
                     .dropped = state.asset_watcher.dropped_event_count(),
                     .waiting = state.asset_watch_result == gneiss::engine::error::not_ready &&
                                !state.asset_watch_failed};
  view.watches[1] = {.root = author_root,
                     .operation = state.author_watch_result,
                     .dropped = state.author_asset_watcher.dropped_event_count(),
                     .waiting = false};
  view.source_rescanning = state.asset_reimports.is_rescanning();
  view.author_rescanning = state.author_assets.is_rescanning();
  view.source_scan_result = state.asset_reimports.rescan_result();
  view.author_scan_result = state.author_assets.rescan_result();
  view.worker_stage = background.stage;
  view.worker_source = worker_source;
  view.worker_error = background.error;
  view.queued = background.pending;
  view.can_cancel_tasks = background.active || background.pending != 0U || background.rescanning;
  view.refresh_failed = state.asset_result != gneiss::editor::asset_browser_result::success;
  view.import_attempted = state.import_attempted;
  view.import_succeeded = state.last_import.result == gneiss::editor::editor_import_result::success;
  view.can_retry_import =
      state.import_attempted && !view.import_succeeded && !state.last_import.source_path.empty();
  view.import_diagnostic = state.last_import.diagnostic;
  const auto selected = std::ranges::find(state.assets.entries(), state.assets.selection(),
                                          &gneiss::editor::asset_browser_entry::id);
  const auto& load = state.runtime.scene_load_status();
  constexpr const char* phases[] = {
      "Preparing description", "Preparing assets", "Verifying sources", "Creating scene",
      "Ready to activate",     "Scene active",     "Load failed",       "Load cancelled"};
  const auto phase = static_cast<unsigned>(load.phase);
  view.scene.enabled = state.runtime.supports_scene_loading();
  view.scene.visible = load.source.revision != 0U;
  view.scene.phase = phase < std::size(phases) ? phases[phase] : "Unknown";
  view.scene.uri = load.source.uri;
  view.scene.message = load.message;
  view.scene.completed = load.completed;
  view.scene.total = load.total;
  view.scene.can_cancel = load.can_cancel;
  view.scene.can_retry = load.phase == gneiss::host::ipc_scene_phase::failed ||
                         load.phase == gneiss::host::ipc_scene_phase::cancelled;
  view.scene.can_load = view.scene.enabled && selected != state.assets.entries().end() &&
                        selected->asset_uri.ends_with(".scene.json") &&
                        (!view.scene.visible || gneiss::host::scene_phase_terminal(load.phase));
  if (load.budget && load.budget->cleanup_pending) {
    view.scene.can_retry = false;
    view.scene.can_load = false;
  }
  if (load.budget) {
    const auto& budget = *load.budget;
    view.scene.budget = gneiss::editor::asset_budget_view{
        .candidate_logical = budget.candidate_logical_bytes,
        .candidate_cpu = budget.candidate_cpu_data_bytes,
        .application_logical = budget.application_logical_bytes,
        .application_cpu = budget.application_cpu_data_bytes,
        .available = budget.available_bytes,
        .cleanup_complete = budget.cleanup_complete,
        .cleanup_pending = budget.cleanup_pending,
    };
  }
  const auto& reload = state.runtime.asset_reload_status();
  view.reload.publish_result = reload.publish_result;
  view.reload.visible = reload.state != runtime_asset_reload_state::idle;
  if (reload.state == runtime_asset_reload_state::applied) {
    view.reload.tone = gneiss::editor::asset_status_tone::success;
  } else if (reload.state == runtime_asset_reload_state::failed ||
             reload.state == runtime_asset_reload_state::restart_required) {
    view.reload.tone = gneiss::editor::asset_status_tone::error;
  }
  view.reload.revision = reload.revision;
  view.reload.message = reload.message;
  view.reload.completed = reload.completed_assets;
  view.reload.total = reload.total_assets;
  for (const auto& item : reload.items) {
    view.reload.items.push_back({item.asset.uri, item.operation, item.applied});
  }
  view.reload.can_cancel = reload.can_cancel;
  view.reload.can_retry = reload.state == runtime_asset_reload_state::failed;
  view.reload.restart_required = reload.state == runtime_asset_reload_state::restart_required;
  const auto& change = state.author_assets.status();
  view.author_change_visible = change.state != gneiss::editor::author_asset_change_state::idle;
  if (change.state == gneiss::editor::author_asset_change_state::applied) {
    view.author_change_tone = gneiss::editor::asset_status_tone::success;
  } else if (change.state == gneiss::editor::author_asset_change_state::conflict ||
             change.state == gneiss::editor::author_asset_change_state::failed) {
    view.author_change_tone = gneiss::editor::asset_status_tone::error;
  }
  view.author_uri = change.uri;
  view.author_message = change.message;
  gneiss::scene_node_id selected_node;
  if (const auto node = state.editing.document().selected_prefab_node())
    selected_node = node->node;
  else if (const auto ordinary_node = state.editing.document().selected_node())
    selected_node = ordinary_node->node;
  const bool has_renderer =
      selected_node && state.world->node_mesh_renderer(selected_node).has_value();
  if (selected != state.assets.entries().end()) {
    view.can_add_mesh = is_mesh_asset(*selected) &&
                        find_material_for_mesh(state.assets.entries(), *selected) != nullptr;
    view.can_edit_material = selected->kind == gneiss::editor::asset_browser_kind::authored_asset &&
                             is_material_asset(*selected) && !state.material_editor.is_dirty() &&
                             !state.material_editor.saving() && !state.pending_file_operation;
    view.can_add_prefab = selected->kind == gneiss::editor::asset_browser_kind::authored_asset &&
                          is_prefab_asset(*selected);
    view.can_apply = has_renderer && !state.editing.assets().active() &&
                     !state.editing.history().is_pending() &&
                     (is_mesh_asset(*selected) || is_material_asset(*selected));
  }
  view.scene_edit_attempted = state.asset_scene_attempted;
  view.scene_edit_result = state.asset_scene_result;
  const auto request = draw_asset_browser_panel(state.assets, state.asset_filter,
                                                state.panel_visibility.asset_browser, view);
  execute_asset_browser_request(state, request);
}

#endif

bool draw_transform_gizmo(editor_state& state, const ImVec2& minimum, const ImVec2& size) noexcept {
  return gneiss::editor::draw_transform_gizmo(
      {
          .session = state.editing.document(),
          .history = state.editing.history(),
          .drag = state.gizmo_drag,
          .camera = state.camera.current_transform(),
          .operation = state.gizmo_mode,
          .wait_release = state.gizmo_wait_release,
          .error = state.history_error,
      },
      minimum, size);
}

gneiss::engine::error update_editor_camera(editor_state& state,
                                           const gneiss::engine::frame_time& time) {
  auto& io = ImGui::GetIO();
  gneiss::editor::editor_camera_input input;
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  input.delta_seconds =
      static_cast<float>(static_cast<double>(time.delta_ns) / nanoseconds_per_second);
  input.move_forward =
      (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0F : 0.0F) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0F : 0.0F);
  input.move_right =
      (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0F : 0.0F) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0F : 0.0F);
  input.move_up =
      (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0F : 0.0F) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0F : 0.0F);
  input.dolly = io.MouseWheel;
  if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    constexpr float look_sensitivity = 0.004F;
    input.yaw_delta = -io.MouseDelta.x * look_sensitivity;
    input.pitch_delta = -io.MouseDelta.y * look_sensitivity;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
    auto selected_node = gneiss::scene_node_id{};
    if (const auto selected = state.editing.document().selected_node(); bool(selected)) {
      selected_node = selected->node;
    } else if (const auto prefab = state.editing.document().selected_prefab_node(); bool(prefab)) {
      selected_node = prefab->node;
    }
    if (selected_node.is_valid()) {
      gneiss::engine::math::transform target;
      const auto result =
          state.application->native_world()->node_get_world_transform(selected_node, target);
      if (result != gneiss::engine::error::success) {
        return result;
      }
      return state.camera.focus({target.translation, target.rotation, target.scale});
    }
  }
  return state.camera.update(input);
}

// 仅冒烟模式采样；显示计数和 GPU 回执来自异步渲染器，不能当作主循环 FPS。
gneiss::engine::error advance_editor_smoke(editor_state& state) {
  if (!state.smoke) {
    return gneiss::engine::error::success;
  }
  if (std::chrono::steady_clock::now() - state.smoke_started > std::chrono::minutes{5}) {
    return gneiss::engine::error::not_ready;
  }
  if (state.scene_loader.visible() && !state.scene_loader.active()) {
    return state.scene_loader.operation();
  }
  if (!state.scene_loader.visible()) {
    ++state.smoke_frames;
    if (state.smoke_frames == 1U || state.smoke_frames % 120U == 0U ||
        state.smoke_frames == state.smoke_frame_limit) {
      const auto stats = state.application->render_statistics();
      const auto now = std::chrono::steady_clock::now();
      if (state.smoke_frames == 1U) {
        state.smoke_scene_started = now;
        state.smoke_presented_start = stats.presented_frames;
      }
      std::fprintf(
          stdout,
          "Editor frame sample: frame=%u elapsed_ms=%.3f presented=%llu interval_ms=%.3f "
          "capture_ms=%.3f queue_ms=%.3f pending=%zu gpu_valid=%u gpu_ms=%.3f "
          "shadow_ms=%.3f opaque_ms=%.3f tone_ms=%.3f\n",
          state.smoke_frames,
          std::chrono::duration<double, std::milli>(now - state.smoke_scene_started).count(),
          static_cast<unsigned long long>(stats.presented_frames - state.smoke_presented_start),
          stats.latest_frame_interval_ms, static_cast<double>(stats.latest_frame_capture_ms),
          static_cast<double>(stats.latest_frame_queue_wait_ms), stats.pending_frames,
          static_cast<unsigned>(stats.latest_gpu_timing_valid),
          static_cast<double>(stats.latest_gpu_frame_ms),
          static_cast<double>(stats.latest_gpu_shadow_ms),
          static_cast<double>(stats.latest_gpu_opaque_ms),
          static_cast<double>(stats.latest_gpu_tone_mapping_ms));
      std::fflush(stdout);
    }
    if (state.smoke_frames >= state.smoke_frame_limit) {
      state.application->request_exit();
    }
  }
  return gneiss::engine::error::success;
}

void draw_scene_loading(editor_state& state) {
  using phase = gneiss::runtime::scene_load_phase;
  const auto& progress = state.scene_loader.progress();
  const char* stage = "Preparing scene";
  switch (progress.phase) {
  case phase::preparing:
    stage = "Preparing scene";
    break;
  case phase::assets:
    stage = "Loading assets";
    break;
  case phase::instantiating:
    stage = "Building scene";
    break;
  case phase::ready:
    stage = "Activating scene";
    break;
  case phase::applied:
    stage = "Scene ready";
    break;
  case phase::failed:
    stage = "Could not load scene";
    break;
  case phase::cancelled:
    stage = "Loading cancelled";
    break;
  }
  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5F, 0.5F});
  ImGui::SetNextWindowSize({520.0F, 0.0F});
  ImGui::Begin("Opening Scene", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  ImGui::TextUnformatted(stage);
  const auto uri = state.scene_loader.uri();
  ImGui::TextWrapped("%.*s", static_cast<int>(uri.size()), uri.data());
  if (state.scene_loader.active()) {
    if (progress.total != 0U) {
      // 仅表示当前阶段，不把未知阶段时长伪装成总进度。
      const auto fraction =
          static_cast<float>(progress.completed) / static_cast<float>(progress.total);
      ImGui::ProgressBar(std::clamp(fraction, 0.0F, 1.0F), {-1.0F, 0.0F});
      ImGui::Text("Current stage: %zu / %zu", progress.completed, progress.total);
    } else {
      ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), {-1.0F, 0.0F}, "Working...");
    }
    if (progress.cleanup_pending) {
      ImGui::TextUnformatted("Finishing pending operations...");
    }
    ImGui::BeginDisabled(!progress.can_cancel);
    if (ImGui::Button("Cancel")) {
      (void)state.scene_loader.cancel(*state.application);
    }
    ImGui::EndDisabled();
  } else {
    if (progress.phase == phase::failed) {
      const auto message = state.scene_loader.operation().message();
      ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
    }
    if (ImGui::Button("Retry")) {
      const std::string retry_uri{uri};
      (void)state.scene_loader.request(*state.application, state.editing, retry_uri);
    }
    ImGui::SameLine();
    if (ImGui::Button("Back to Editor")) {
      state.scene_loader.dismiss();
    }
  }
  ImGui::End();
}

gneiss::engine::error update_editor(const gneiss::engine::frame_time* time, void* user_data) {
  try {
    if (!time || !user_data) {
      return gneiss::engine::error::invalid_argument;
    }
    auto& state = *static_cast<editor_state*>(user_data);
    state.runtime.update();
    if (state.layout.loading()) {
      const auto status = state.layout.advance(state.panel_visibility);
      if (status.failed() && status != gneiss::engine::error::not_ready) {
        state.layout_status = status;
        std::fprintf(stderr, "Gneiss Editor 布局读取失败：%d\n", status.code());
      }
    }
    if (state.pending_file_operation) {
      const bool saving = state.file_operation_saves_scene;
      bool close_save_dialogs{};
      auto pending = std::move(state.pending_file_operation);
      gneiss::engine::error status;
      if (!pending(status))
        state.pending_file_operation = std::move(pending);
      else if (!state.pending_file_operation) {
        if (saving) {
          state.save_result = status;
          state.save_attempted = true;
          if (status.ok() && !state.editing.document().is_dirty()) {
            close_save_dialogs = true;
            const auto action = state.document_actions.saved(status);
            if (action != gneiss::editor::document_action::none)
              state.history_error = perform_document_action(state, action);
            if (state.run_after_save) {
              state.runtime_result = launch_runtime(state, false);
              state.pending_save_and_run = false;
            }
          }
          state.run_after_save = false;
        } else {
          state.prefab_author_result = status;
          state.prefab_author_attempted = true;
        }
      }
      // 本帧只显示等待界面；防止完成同帧的输入落到已切换文档。
      const auto begun = state.ui.begin_frame(*state.application, *time);
      if (begun.failed())
        return begun;
      if (close_save_dialogs) {
        for (const auto* name : {"Unsaved Changes", "Save and Run"}) {
          if (ImGui::BeginPopupModal(name)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
          }
        }
      }
      ImGui::Begin("File Operation", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::TextUnformatted(state.pending_file_operation ? "正在读取或保存文件……"
                                                          : "文件操作已完成");
      ImGui::End();
      return submit_editor_ui(state);
    }
    if (state.task_scheduler.mode() == gneiss::engine::execution_mode::cooperative) {
      (void)state.task_scheduler.run_ready();
    }
    bool scene_changed{};
    const auto loading = state.scene_loader.advance(*state.application, state.editing, state.world,
                                                    state.camera, scene_changed);
    if (loading != gneiss::engine::error::success) {
      return loading;
    }
    if (state.closing) {
      state.editing.advance_cleanup(*state.application);
      state.package_process.update();
      bool stopped = !state.editing.active() && !state.scene_loader.active() &&
                     !state.runtime.is_running() && !state.package_process.is_running();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
      std::vector<std::filesystem::path> ignored;
      (void)state.author_assets.poll_rescan(ignored);
      (void)state.asset_reimports.poll_browser(state.assets, state.asset_result);
      stopped = stopped && state.asset_reimports.status().stopped && state.author_assets.stopped();
#endif
      if (!state.layout_save_started) {
        state.layout_save_started = true;
        state.layout_status = state.smoke ? gneiss::engine::error::success
                                          : state.layout.save(state.panel_visibility);
      }
      if (!state.smoke && !state.layout.saving() && state.layout_status.ok())
        state.layout_status = state.layout.save_result();
      if (stopped && !state.layout.saving())
        state.application->request_exit();
      const auto begun = state.ui.begin_frame(*state.application, *time);
      if (begun.failed())
        return begun;
      ImGui::Begin("Closing Editor");
      ImGui::TextUnformatted("正在保存布局并收尾后台任务……");
      ImGui::End();
      return submit_editor_ui(state);
    }
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    if (state.author_reload_uri && !state.scene_loader.active()) {
      auto operation = state.scene_loader.operation();
      if (operation.ok())
        operation = state.runtime.publish_asset_revision(
            std::span<const std::string>(&*state.author_reload_uri, 1U));
      if (operation.ok()) {
        state.author_assets.mark_applied(*state.author_reload_uri);
        state.asset_reimports.request_refresh();
      } else {
        state.author_assets.mark_failed(*state.author_reload_uri, operation);
      }
      state.author_reload_uri.reset();
    }
#endif
    if (scene_changed) {
      state.gizmo_drag = {};
      state.gizmo_wait_release = false;
      gneiss::editor::reset_transform_gizmo();
      state.rename_uuid = {};
      state.rename_previous.clear();
      state.pending_hierarchy_action = hierarchy_action::none;
      state.pending_hierarchy_uuid = {};
      state.pending_prefab_author_action = prefab_author_action::none;
      state.pending_prefab_instance_uuid = {};
      state.pending_prefab_source_uuid = {};
      state.pending_prefab_root_uuid = {};
      if (state.smoke) {
        auto query = state.editing.document().capture_nodes();
        if (!query)
          return query.error();
        std::fprintf(stdout, "Editor scene ready: nodes=%zu, prefab_nodes=%zu\n",
                     query->nodes.size(), query->prefab_nodes.size());
        std::fflush(stdout);
      }
    }
    const auto smoke_result = advance_editor_smoke(state);
    if (smoke_result != gneiss::engine::error::success) {
      return smoke_result;
    }
    state.editing.prefabs().advance();
    if (state.editing.history().is_pending()) {
      const auto operation = state.editing.history().advance_pending();
      if (!state.editing.history().is_pending()) {
        state.asset_scene_result = operation;
        synchronize_history_dirty(state);
      }
    }
    if (state.editing.prefabs().visible() || state.editing.history().is_pending()) {
      const auto begun = state.ui.begin_frame(*state.application, *time);
      if (begun != gneiss::engine::error::success) {
        return begun;
      }
      const auto* viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5F, 0.5F});
      ImGui::SetNextWindowSize({520.0F, 0.0F});
      ImGui::Begin("Prefab Operation", nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
      if (state.editing.prefabs().active() || state.editing.history().is_pending()) {
        const char* stage = "Updating scene";
        if (state.editing.prefabs().active()) {
          using phase = gneiss::editor::prefab_edit_phase;
          switch (state.editing.prefabs().phase()) {
          case phase::preparing:
            stage = "Reading Prefab";
            break;
          case phase::assets:
            stage = "Preparing assets";
            break;
          case phase::constructing:
            stage = "Building Prefab";
            break;
          case phase::cleanup:
            stage = "Finishing cleanup";
            break;
          case phase::finished:
            break;
          }
        } else if (!state.editing.history().can_cancel_pending()) {
          stage = "Finishing cleanup";
        }
        ImGui::TextUnformatted(stage);
        ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), {-1.0F, 0.0F}, "Working...");
        const bool can_cancel = state.editing.history().is_pending()
                                    ? state.editing.history().can_cancel_pending()
                                    : state.editing.prefabs().can_cancel();
        if (can_cancel && ImGui::Button("Cancel")) {
          state.editing.prefabs().cancel();
          state.editing.history().cancel_pending();
        }
      } else {
        const auto message = state.editing.prefabs().operation().message();
        ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
        if (ImGui::Button("Back to Editor")) {
          state.editing.prefabs().dismiss();
        }
      }
      ImGui::End();
      return submit_editor_ui(state);
    }
    state.editing.assets().advance(*state.application, state.editing.document(),
                                   state.editing.history());
    if (state.editing.assets().visible()) {
      const auto begun = state.ui.begin_frame(*state.application, *time);
      if (begun != gneiss::engine::error::success)
        return begun;
      const auto* viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5F, 0.5F});
      ImGui::SetNextWindowSize({520.0F, 0.0F});
      ImGui::Begin("Loading Assets", nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
      const auto& progress = state.editing.assets().progress();
      if (state.editing.assets().active()) {
        ImGui::TextUnformatted("读取与解码资源");
        if (progress.total_assets != 0U) {
          ImGui::ProgressBar(static_cast<float>(progress.completed_assets) /
                                 static_cast<float>(progress.total_assets),
                             {-1.0F, 0.0F});
        } else {
          ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), {-1.0F, 0.0F}, "Working...");
        }
        if (ImGui::Button("Cancel"))
          state.editing.assets().cancel(*state.application);
      } else {
        const auto message = state.editing.assets().operation().message();
        ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
        if (ImGui::Button("Back to Editor"))
          state.editing.assets().dismiss();
      }
      ImGui::End();
      return submit_editor_ui(state);
    }
    if (state.scene_loader.visible()) {
      const auto begun = state.ui.begin_frame(*state.application, *time);
      if (begun != gneiss::engine::error::success) {
        return begun;
      }
      draw_scene_loading(state);
      return submit_editor_ui(state);
    }
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    start_source_asset_watch(state);
    std::vector<gneiss::editor::asset_file_event> file_events;
    (void)state.asset_watcher.poll_events(file_events);
    for (const auto& event : file_events) {
      if (event.kind == gneiss::editor::asset_file_event_kind::error) {
        if (!state.asset_watch_failed) {
          state.asset_reimports.request_rescan();
        }
        state.asset_watch_result = event.operation;
        state.asset_watch_failed = true;
      } else {
        (void)state.asset_reimports.notify(event.relative_path);
      }
    }
    std::vector<gneiss::editor::asset_file_event> author_events;
    (void)state.author_asset_watcher.poll_events(author_events);
    const auto author_drops = state.author_asset_watcher.dropped_event_count();
    if (author_drops != state.observed_author_drops) {
      state.observed_author_drops = author_drops;
      state.author_assets.request_rescan();
    }
    std::vector<std::filesystem::path> author_candidates;
    (void)state.author_assets.poll_rescan(author_candidates);
    for (auto& candidate : author_candidates) {
      author_events.push_back({.relative_path = std::move(candidate)});
    }
    for (auto& event : author_events)
      state.pending_author_events.push_back(std::move(event));
    while (!state.pending_author_events.empty()) {
      auto event = std::move(state.pending_author_events.front());
      state.pending_author_events.pop_front();
      if (event.kind == gneiss::editor::asset_file_event_kind::error) {
        if (state.author_watch_result == gneiss::engine::error::success) {
          state.author_assets.request_rescan();
        }
        state.author_watch_result = event.operation;
        continue;
      }
      auto query = state.editing.document().capture_nodes();
      if (!query) {
        state.author_watch_result = query.error();
        continue;
      }
      const auto candidate_uri = "asset://" + path_utf8(event.relative_path.lexically_normal());
      const auto affects_open_document =
          candidate_uri == state.editing.document().uri() ||
          std::ranges::any_of(query->prefab_nodes, [&candidate_uri](const auto& node) {
            return node.prefab_uri == candidate_uri;
          });
      const auto change = state.author_assets.observe(
          event.relative_path, state.editing.document().is_dirty() && affects_open_document);
      if (change.state != gneiss::editor::author_asset_change_state::changed) {
        continue;
      }
      if (change.operation != gneiss::engine::error::success) {
        state.author_assets.mark_failed(change.uri, change.operation);
        continue;
      }
      const auto is_current_scene = change.uri == state.editing.document().uri();
      const auto is_used_prefab =
          std::ranges::any_of(query->prefab_nodes, [&change](const auto& node) {
            return node.prefab_uri == change.uri;
          });
      auto operation = gneiss::engine::error::success;
      if (is_current_scene || is_used_prefab) {
        operation = state.scene_loader.request(*state.application, state.editing,
                                               state.editing.document().uri());
        if (operation.ok()) {
          state.author_reload_uri = change.uri;
          // 暂停消费后续变更，保留本次指纹，加载完成后才确认并通知 Runtime。
          break;
        }
        if (operation == gneiss::engine::error::not_ready) {
          state.pending_author_events.push_front(std::move(event));
          break;
        }
      }
      if (operation == gneiss::engine::error::success) {
        operation =
            state.runtime.publish_asset_revision(std::span<const std::string>(&change.uri, 1U));
      }
      if (operation == gneiss::engine::error::success) {
        state.author_assets.mark_applied(change.uri);
        state.asset_reimports.request_refresh();
      } else {
        state.author_assets.mark_failed(change.uri, operation);
      }
    }
    const auto source_drops = state.asset_watcher.dropped_event_count();
    const auto candidate_drops = state.asset_reimports.dropped_candidate_count();
    if (source_drops != state.observed_source_drops ||
        candidate_drops != state.observed_candidate_drops) {
      state.observed_source_drops = source_drops;
      state.observed_candidate_drops = candidate_drops;
      state.asset_reimports.request_rescan();
    }
    (void)state.asset_reimports.poll_browser(state.assets, state.asset_result);
    state.asset_reimports.set_paused(state.show_package_dialog ||
                                     state.package_process.is_running());
    std::vector<gneiss::editor::asset_reimport_event> reimport_events;
    (void)state.asset_reimports.poll_events(reimport_events);
    for (auto& event : reimport_events) {
      if (event.state == gneiss::editor::asset_reimport_state::succeeded) {
        (void)state.runtime.publish_asset_revision(event.import.import.output_uris);
      }
      if (event.state == gneiss::editor::asset_reimport_state::succeeded ||
          event.state == gneiss::editor::asset_reimport_state::failed) {
        state.last_import = std::move(event.import);
        state.import_attempted = true;
      }
    }
#endif
    if (state.scene_loader.active()) {
      const auto begun = state.ui.begin_frame(*state.application, *time);
      if (begun.failed())
        return begun;
      draw_scene_loading(state);
      return submit_editor_ui(state);
    }
    if (state.runtime_attempted) {
      state.runtime_result = state.runtime.last_result();
    }
    if (state.package_process.has_started()) {
      state.package_process.update();
      if (!state.package_process.is_running()) {
        state.package_result = state.package_process.exit_code() == 0
                                   ? gneiss::engine::error::success
                                   : gneiss::engine::error::dependency_failed;
      }
    }
    auto result = state.ui.begin_frame(*state.application, *time);
    if (result != gneiss::engine::error::success) {
      return result;
    }
    gneiss::editor::begin_transform_gizmo_frame();
    // 先收尾再处理菜单、保存和撤销；折叠或隐藏面板也不能遗失已应用的拖动。
    if (state.gizmo_drag.is_active() &&
        (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::GetIO().KeyCtrl ||
         ImGui::IsKeyPressed(ImGuiKey_Delete, false))) {
      state.history_error =
          state.gizmo_drag.finish(state.editing.document(), state.editing.history());
      state.gizmo_wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      gneiss::editor::reset_transform_gizmo();
    }
    const auto selection_result = state.editing.document().validate_selection();
    if (selection_result != gneiss::engine::error::success &&
        selection_result != gneiss::engine::error::invalid_handle) {
      return selection_result;
    }

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        const auto new_requested = ImGui::MenuItem("New Scene", "Ctrl+N");
        const auto open_requested = ImGui::MenuItem("Open Scene...", "Ctrl+O");
        ImGui::Separator();
        const auto save_requested = ImGui::MenuItem("Save", "Ctrl+S");
        const auto save_as_requested = ImGui::MenuItem("Save As...", "Ctrl+Shift+S");
        ImGui::Separator();
#if defined(GNEISS_EDITOR_HAS_RUNTIME)
        ImGui::BeginDisabled(state.editing.document().is_dirty() || state.runtime.is_busy() ||
                             state.package_process.is_running());
        const auto package_requested = ImGui::MenuItem("Export Package...");
        ImGui::EndDisabled();
        ImGui::Separator();
#endif
        const auto exit_requested = ImGui::MenuItem("Exit");
        if (save_requested) {
          state.save_result = save_document(state);
          state.save_attempted = state.save_result != gneiss::engine::error::not_ready;
        }
        if (save_as_requested) {
          state.save_result = save_document_as(state);
          state.save_attempted = state.save_result != gneiss::engine::error::not_ready;
        }
#if defined(GNEISS_EDITOR_HAS_RUNTIME)
        if (package_requested) {
          const auto suggested = state.project_root.parent_path() /
                                 (state.project_root.filename().string() + "-development");
          const auto text = suggested.string();
          std::snprintf(state.package_output.data(), state.package_output.size(), "%s",
                        text.c_str());
          state.show_package_dialog = true;
          ImGui::OpenPopup("Export Package");
        }
#endif
        gneiss::editor::document_action requested = gneiss::editor::document_action::none;
        if (new_requested) {
          requested = gneiss::editor::document_action::new_scene;
        } else if (open_requested) {
          requested = gneiss::editor::document_action::open_scene;
        } else if (exit_requested) {
          requested = gneiss::editor::document_action::exit_editor;
        }
        if (requested != gneiss::editor::document_action::none) {
          request_document_action(state, requested);
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Edit")) {
        ImGui::BeginDisabled(!state.editing.history().can_undo());
        const auto undo_requested = ImGui::MenuItem("Undo", "Ctrl+Z");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!state.editing.history().can_redo());
        const auto redo_requested = ImGui::MenuItem("Redo", "Ctrl+Shift+Z");
        ImGui::EndDisabled();
        if (undo_requested) {
          state.history_error = undo_editor_command(state);
        }
        if (redo_requested) {
          state.history_error = redo_editor_command(state);
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Run")) {
        ImGui::BeginDisabled(state.runtime.is_busy());
        const auto run_requested = ImGui::MenuItem("Run Project", "F6");
        ImGui::EndDisabled();
        const auto control_state = state.runtime.control_state();
        const auto can_toggle_pause =
            control_state == gneiss::host::runtime_control_state::running ||
            control_state == gneiss::host::runtime_control_state::paused;
        ImGui::BeginDisabled(!can_toggle_pause);
        const auto pause_requested = ImGui::MenuItem(
            control_state == gneiss::host::runtime_control_state::paused ? "Resume" : "Pause",
            "F7");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!state.runtime.is_busy());
        const auto stop_requested = ImGui::MenuItem("Stop", "F8");
        ImGui::EndDisabled();
        if (run_requested) {
          request_runtime_launch(state);
        }
        if (pause_requested) {
          state.runtime_result = control_state == gneiss::host::runtime_control_state::paused
                                     ? state.runtime.request_resume()
                                     : state.runtime.request_pause();
          state.runtime_attempted = true;
        }
        if (stop_requested) {
          state.runtime_result = state.runtime.request_stop();
          state.runtime_attempted = true;
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Scene Hierarchy", nullptr, &state.panel_visibility.scene_hierarchy);
        ImGui::MenuItem("Asset Browser", nullptr, &state.panel_visibility.asset_browser);
        ImGui::MenuItem("Scene View", nullptr, &state.panel_visibility.scene_view);
        ImGui::MenuItem("Inspector", nullptr, &state.panel_visibility.inspector);
        ImGui::MenuItem("Console", nullptr, &state.panel_visibility.console);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) {
          state.panel_visibility = {};
          gneiss::editor::reset_editor_layout();
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Development")) {
        ImGui::MenuItem("ImGui Demo", nullptr, &state.show_imgui_demo);
        ImGui::EndMenu();
      }

      constexpr float toolbar_width = 92.0F;
      const auto toolbar_x = (ImGui::GetWindowWidth() - toolbar_width) * 0.5F;
      if (toolbar_x > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(toolbar_x);
      }
      const auto runtime_busy = state.runtime.is_busy();
      const auto runtime_control = state.runtime.control_state();
      if (gneiss::editor::toolbar_icon_button("##RunProject", gneiss::editor::toolbar_icon::run,
                                              "运行工程 (F6)", !runtime_busy,
                                              state.runtime.is_running())) {
        request_runtime_launch(state);
      }
      ImGui::SameLine(0.0F, 4.0F);
      const auto can_toggle_pause =
          runtime_control == gneiss::host::runtime_control_state::running ||
          runtime_control == gneiss::host::runtime_control_state::paused;
      if (gneiss::editor::toolbar_icon_button(
              "##PauseRuntime", gneiss::editor::toolbar_icon::pause,
              runtime_control == gneiss::host::runtime_control_state::paused ? "恢复 (F7)"
                                                                             : "暂停 (F7)",
              can_toggle_pause, runtime_control == gneiss::host::runtime_control_state::paused)) {
        state.runtime_result = runtime_control == gneiss::host::runtime_control_state::paused
                                   ? state.runtime.request_resume()
                                   : state.runtime.request_pause();
        state.runtime_attempted = true;
      }
      ImGui::SameLine(0.0F, 4.0F);
      if (gneiss::editor::toolbar_icon_button("##StopRuntime", gneiss::editor::toolbar_icon::stop,
                                              "停止 (F8)", runtime_busy)) {
        state.runtime_result = state.runtime.request_stop();
        state.runtime_attempted = true;
      }
      ImGui::EndMainMenuBar();
    }
    if (state.editing.history().is_pending() || state.pending_file_operation) {
      return submit_editor_ui(state);
    }
    gneiss::editor::begin_editor_workspace();
    bool material_focus = false;
    gneiss::editor::draw_material_editor(state.material_editor, *state.application->async_files(),
                                         state.material_editor_visible, state.material_edit_serial,
                                         state.material_edit_status, material_focus);
    const auto& io = ImGui::GetIO();
    if (!material_focus && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
      request_document_action(state, gneiss::editor::document_action::new_scene);
    }
    if (!material_focus && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
      request_document_action(state, gneiss::editor::document_action::open_scene);
    }
    if (!material_focus && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      state.save_result = save_document_as(state);
      state.save_attempted = state.save_result != gneiss::engine::error::not_ready;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F6, false) && !state.runtime.is_busy()) {
      request_runtime_launch(state);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false) && state.runtime.is_busy()) {
      state.runtime_result = state.runtime.request_stop();
      state.runtime_attempted = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) {
      const auto control = state.runtime.control_state();
      if (control == gneiss::host::runtime_control_state::running ||
          control == gneiss::host::runtime_control_state::paused) {
        state.runtime_result = control == gneiss::host::runtime_control_state::paused
                                   ? state.runtime.request_resume()
                                   : state.runtime.request_pause();
        state.runtime_attempted = true;
      }
    }
    if (state.pending_save_and_run && !ImGui::IsPopupOpen("Save and Run")) {
      ImGui::OpenPopup("Save and Run");
    }
#if defined(GNEISS_EDITOR_HAS_RUNTIME)
    if (state.show_package_dialog && !ImGui::IsPopupOpen("Export Package")) {
      ImGui::OpenPopup("Export Package");
    }
    if (state.show_package_dialog &&
        ImGui::BeginPopupModal("Export Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      constexpr const char* profiles[] = {"Debug", "Development", "Shipping"};
      ImGui::SetNextItemWidth(480.0F);
      ImGui::InputText("Output", state.package_output.data(), state.package_output.size());
      ImGui::Combo("Profile", &state.package_profile, profiles,
                   static_cast<int>(std::size(profiles)));
      ImGui::Checkbox("Create ZIP", &state.package_zip);
      if (state.package_process.is_running()) {
        ImGui::TextUnformatted("正在配置、构建并生成发布包……");
      } else {
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
        state.asset_reimports.set_paused(true);
        const auto assets_busy = state.asset_reimports.status().active;
        if (assets_busy) {
          ImGui::TextUnformatted("Waiting for asset task to finish...");
        }
        ImGui::BeginDisabled(assets_busy);
#endif
        if (ImGui::Button("Export")) {
          static constexpr std::array<std::string_view, 3U> profile_names = {"debug", "development",
                                                                             "shipping"};
          gneiss::engine::child_process_start_info info;
          info.executable = GNEISS_EDITOR_PROJECT_PATH;
          info.working_directory = state.project_root;
          info.arguments = {"package", state.project_root, GNEISS_EDITOR_RUNTIME_PATH,
                            state.package_output.data(),
                            profile_names[static_cast<std::size_t>(state.package_profile)]};
          if (state.package_zip) {
            info.arguments.emplace_back("--zip");
          }
          state.package_process.clear_output();
          state.package_result = state.package_process.start(info);
          state.package_attempted = true;
        }
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
        ImGui::EndDisabled();
#endif
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
          state.show_package_dialog = false;
          ImGui::CloseCurrentPopup();
        }
      }
      if (state.package_attempted && !state.package_process.is_running()) {
        const auto message = state.package_result.message();
        ImGui::Text("Result: %.*s", static_cast<int>(message.size()), message.data());
      }
      const auto& output = state.package_process.output();
      if (!output.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", output.c_str());
      }
      ImGui::EndPopup();
    }
#endif
    if (state.pending_save_and_run &&
        ImGui::BeginPopupModal("Save and Run", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Save the current scene before running the project?");
      if (ImGui::Button("Save and Run")) {
        state.runtime_result = launch_runtime(state, true);
        state.runtime_attempted = true;
        if (state.runtime_result == gneiss::engine::error::success) {
          state.pending_save_and_run = false;
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        state.pending_save_and_run = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    gneiss::editor::draw_document_action_panel(
        state.document_actions,
        {
            .context = &state,
            .save =
                [](void* context) {
                  auto& editor = *static_cast<editor_state*>(context);
                  editor.save_result = save_document(editor);
                  editor.save_attempted = editor.save_result != gneiss::engine::error::not_ready;
                  return editor.save_result;
                },
            .perform =
                [](void* context, gneiss::editor::document_action action) {
                  auto& editor = *static_cast<editor_state*>(context);
                  editor.history_error = perform_document_action(editor, action);
                },
        });
    if (state.pending_file_operation)
      return submit_editor_ui(state);
    if (!material_focus && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      state.history_error = io.KeyShift ? redo_editor_command(state) : undo_editor_command(state);
    }
    if (state.editing.history().is_pending() || state.pending_file_operation) {
      return submit_editor_ui(state);
    }

    const auto log_path = state.runtime.log_file().generic_string();
    const gneiss::editor::console_runtime_view console_runtime{
        .running = state.runtime.is_running(),
        .started = state.runtime.has_started(),
        .exit_code = state.runtime.exit_code(),
        .attempted = state.runtime_attempted,
        .operation = state.runtime_result,
        .log_path = log_path};
    gneiss::editor::draw_console_panel(
        state.runtime.console(), state.console_panel, state.panel_visibility.console,
        console_runtime, {.context = &state.runtime, .clear = [](void* context) {
                            static_cast<gneiss::host::runtime_process*>(context)->clear_output();
                          }});

    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0F, 180.0F), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("Scene Hierarchy", &state.panel_visibility.scene_hierarchy);
    const auto& runtime_nodes = state.runtime.scene_mirror().nodes();
    if (state.runtime.is_busy() || !runtime_nodes.empty()) {
      if (ImGui::CollapsingHeader("Runtime (Read-only)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& statistics = state.runtime.statistics();
        if (statistics.session_id == state.runtime.scene_mirror().session_id() &&
            statistics.sequence != 0U) {
          const auto frame_ms = static_cast<double>(statistics.frame_delta_ns) / 1'000'000.0;
          const auto frames_per_second =
              statistics.frame_delta_ns == 0U
                  ? 0.0
                  : 1'000'000'000.0 / static_cast<double>(statistics.frame_delta_ns);
          ImGui::Text("Frame: %.2f ms (%.1f FPS)", frame_ms, frames_per_second);
          ImGui::Text("Nodes: %llu | Entities: %llu",
                      static_cast<unsigned long long>(statistics.scene_node_count),
                      static_cast<unsigned long long>(statistics.entity_count));
          ImGui::Text("IPC pending: %llu | dropped events: %llu",
                      static_cast<unsigned long long>(statistics.ipc_pending_writes),
                      static_cast<unsigned long long>(statistics.ipc_dropped_events));
          ImGui::Separator();
        }
        if (runtime_nodes.empty()) {
          ImGui::TextDisabled("Waiting for Runtime scene snapshot");
        } else {
          gneiss::editor::draw_runtime_hierarchy(state.runtime.scene_mirror(),
                                                 state.runtime_selection);
        }
      }
      ImGui::SeparatorText("Author Scene");
    }
    if (!state.editing.document().is_open()) {
      ImGui::TextUnformatted("No scene is open");
    } else {
      auto pending_action = state.pending_hierarchy_action;
      if (pending_action != hierarchy_action::none) {
        if (const auto pending = state.editing.document().find_node(state.pending_hierarchy_uuid);
            bool(pending)) {
          (void)state.editing.document().select(pending->node);
        } else {
          pending_action = hierarchy_action::none;
        }
        state.pending_hierarchy_action = hierarchy_action::none;
        state.pending_hierarchy_uuid = {};
      }
      const auto selected = state.editing.document().selected_node();
      const auto can_edit_selection = bool(selected);
      const auto hierarchy_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
      const auto keyboard_enabled = hierarchy_focused && !ImGui::GetIO().WantTextInput;
      const auto create_requested = ImGui::Button("Create Empty");
      ImGui::SameLine();
      ImGui::BeginDisabled(!selected);
      const auto duplicate_requested =
          ImGui::Button("Duplicate") || pending_action == hierarchy_action::duplicate ||
          (can_edit_selection && keyboard_enabled && ImGui::GetIO().KeyCtrl &&
           ImGui::IsKeyPressed(ImGuiKey_D, false));
      const auto rename_requested =
          ImGui::Button("Rename") || pending_action == hierarchy_action::rename ||
          (can_edit_selection && keyboard_enabled && ImGui::IsKeyPressed(ImGuiKey_F2, false));
      ImGui::SameLine();
      const auto delete_requested =
          ImGui::Button("Delete") || pending_action == hierarchy_action::remove ||
          (can_edit_selection && keyboard_enabled && ImGui::IsKeyPressed(ImGuiKey_Delete, false));
      ImGui::EndDisabled();
      if (rename_requested) {
        state.rename_uuid = selected->uuid;
        state.rename_previous = selected->display_name;
        state.rename_buffer.fill('\0');
        const auto length = std::min(state.rename_previous.size(), state.rename_buffer.size() - 1U);
        std::ranges::copy_n(state.rename_previous.begin(), length, state.rename_buffer.begin());
        ImGui::OpenPopup("Rename Node");
      }
      if (ImGui::BeginPopup("Rename Node")) {
        ImGui::InputText("Name", state.rename_buffer.data(), state.rename_buffer.size());
        if (ImGui::Button("Apply")) {
          const auto next = std::string{state.rename_buffer.data()};
          const auto current = state.editing.document().find_node(state.rename_uuid);
          state.history_error = !current
                                    ? gneiss::engine::error::not_found
                                    : state.editing.document().rename_node(current->node, next);
          if (state.history_error == gneiss::engine::error::success) {
            const auto uuid = state.rename_uuid;
            const auto previous = state.rename_previous;
            state.history_error = state.editing.history().record(
                {.label = "重命名节点",
                 .undo =
                     [&state, uuid, previous] {
                       const auto node = state.editing.document().find_node(uuid);
                       return !node ? gneiss::engine::error::not_found
                                    : state.editing.document().rename_node(node->node, previous);
                     },
                 .redo =
                     [&state, uuid, next] {
                       const auto node = state.editing.document().find_node(uuid);
                       return !node ? gneiss::engine::error::not_found
                                    : state.editing.document().rename_node(node->node, next);
                     },
                 .merge_key = {}});
            if (state.history_error != gneiss::engine::error::success) {
              if (const auto node = state.editing.document().find_node(uuid); bool(node)) {
                (void)state.editing.document().rename_node(node->node, previous);
              }
            }
          }
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      if (create_requested) {
        const auto parent = !selected ? gneiss::scene_node_id{} : selected->node;
        gneiss::scene_node_id created;
        state.history_error = state.editing.document().create_node("Node", parent, created);
        if (state.history_error == gneiss::engine::error::success) {
          const auto uuid = state.editing.document().selected_node()->uuid;
          auto snapshot = std::make_shared<gneiss::editor::scene_subtree_snapshot>();
          state.history_error = state.editing.history().record(
              {.label = "创建节点",
               .undo =
                   [&state, uuid, snapshot] {
                     const auto current = state.editing.document().find_node(uuid);
                     return !current ? gneiss::engine::error::not_found
                                     : state.editing.document().destroy_subtree(current->node,
                                                                                *snapshot);
                   },
               .redo =
                   [&state, snapshot] {
                     gneiss::scene_node_id restored;
                     return state.editing.document().restore_subtree(*snapshot, restored);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::engine::error::success) {
            if (const auto current = state.editing.document().find_node(uuid); bool(current)) {
              (void)state.editing.document().destroy_subtree(current->node, *snapshot);
            }
          }
        }
      }
      if (duplicate_requested) {
        const auto parent = selected->parent;
        gneiss::scene_node_id duplicate;
        state.history_error =
            state.editing.document().duplicate_subtree(selected->node, parent, duplicate);
        if (state.history_error == gneiss::engine::error::success) {
          const auto uuid = state.editing.document().selected_node()->uuid;
          auto snapshot = std::make_shared<gneiss::editor::scene_subtree_snapshot>();
          state.history_error = state.editing.history().record(
              {.label = "复制子树",
               .undo =
                   [&state, uuid, snapshot] {
                     const auto current = state.editing.document().find_node(uuid);
                     return !current ? gneiss::engine::error::not_found
                                     : state.editing.document().destroy_subtree(current->node,
                                                                                *snapshot);
                   },
               .redo =
                   [&state, snapshot] {
                     gneiss::scene_node_id restored;
                     return state.editing.document().restore_subtree(*snapshot, restored);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::engine::error::success) {
            if (const auto current = state.editing.document().find_node(uuid); bool(current)) {
              (void)state.editing.document().destroy_subtree(current->node, *snapshot);
            }
          }
        }
      }
      if (delete_requested) {
        const auto was_dirty = state.editing.document().is_dirty();
        const auto node = selected->node;
        gneiss::editor::scene_subtree_snapshot snapshot;
        state.history_error = state.editing.document().destroy_subtree(node, snapshot);
        if (state.history_error == gneiss::engine::error::success) {
          state.history_error = state.editing.history().record(
              {.label = "删除节点",
               .undo =
                   [&state, snapshot] {
                     gneiss::scene_node_id restored;
                     return state.editing.document().restore_subtree(snapshot, restored);
                   },
               .redo =
                   [&state, uuid = snapshot.root_uuid] {
                     const auto current = state.editing.document().find_node(uuid);
                     if (!current) {
                       return gneiss::engine::error::not_found;
                     }
                     gneiss::editor::scene_subtree_snapshot discarded;
                     return state.editing.document().destroy_subtree(current->node, discarded);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::engine::error::success) {
            gneiss::scene_node_id restored;
            (void)state.editing.document().restore_subtree(snapshot, restored);
            if (!was_dirty) {
              state.editing.document().clear_dirty();
            }
          }
        }
      }
      const auto prefab_selected = state.editing.document().selected_prefab_node();
      if (bool(prefab_selected) && prefab_selected->is_instance_root) {
        if (ImGui::Button("Duplicate Prefab")) {
          const auto original = *prefab_selected;
          gneiss::scene_node_id duplicate;
          state.history_error =
              state.editing.document().duplicate_subtree(original.node, original.parent, duplicate);
          if (state.history_error.ok()) {
            auto snapshot = std::make_shared<gneiss::engine::scene_subtree_snapshot>();
            auto scenes_result = state.application->scenes()->capture_subtree(
                state.editing.document().scene_handle(), duplicate);
            state.history_error =
                scenes_result ? gneiss::engine::error::success : scenes_result.error();
            if (scenes_result) {
              *snapshot = std::move(*scenes_result);
            }
            if (state.history_error.ok()) {
              state.history_error = state.editing.history().record(
                  {.label = "复制子树",
                   .undo =
                       [&state, snapshot] {
                         gneiss::scene_node_id root;
                         auto status_value = state.application->scenes()->find_node(
                             state.editing.document().scene_handle(), snapshot->root_uuid);
                         if (!status_value)
                           return status_value.error();
                         root = std::move(*status_value);

                         gneiss::editor::scene_subtree_snapshot discarded;
                         return state.editing.document().destroy_subtree(root, discarded);
                       },
                   .redo =
                       [&state, snapshot] {
                         gneiss::scene_node_id root;
                         return state.editing.document().restore_subtree(*snapshot, root);
                       },
                   .merge_key = {}});
            }
            if (state.history_error.failed()) {
              gneiss::editor::scene_subtree_snapshot discarded;
              (void)state.editing.document().destroy_subtree(duplicate, discarded);
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Prefab")) {
          state.history_error = state.editing.prefabs().request_remove(
              *state.application, state.editing.document(), state.editing.history(),
              prefab_selected->node);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Prefab") && !state.editing.prefabs().active()) {
          state.history_error = state.editing.prefabs().request_refresh(
              *state.application, state.task_scheduler, state.editing.document(),
              state.editing.history(), prefab_selected->node);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(state.runtime.is_busy());
        if (ImGui::Button("Unpack Prefab...")) {
          state.pending_prefab_author_action = prefab_author_action::unpack;
          state.pending_prefab_instance_uuid = prefab_selected->instance_uuid;
        }
        ImGui::EndDisabled();
      }
      auto query = state.editing.document().capture_nodes();
      gneiss::editor::author_hierarchy_request tree_request;
      if (query) {
        tree_request = gneiss::editor::draw_author_hierarchy({
            .nodes = query->nodes,
            .prefab_nodes = query->prefab_nodes,
            .selection = state.editing.document().selection(),
            .can_create_prefab = !state.runtime.is_busy(),
        });
      } else {
        state.history_error = query.error();
        ImGui::TextUnformatted("Scene query failed");
      }
      using tree_action = gneiss::editor::author_hierarchy_action;
      switch (tree_request.action) {
      case tree_action::none:
        break;
      case tree_action::select:
        (void)state.editing.document().select(tree_request.node);
        state.runtime_selection = {};
        break;
      case tree_action::rename:
      case tree_action::duplicate:
      case tree_action::remove:
        if (tree_request.action == tree_action::rename) {
          state.pending_hierarchy_action = hierarchy_action::rename;
        } else if (tree_request.action == tree_action::duplicate) {
          state.pending_hierarchy_action = hierarchy_action::duplicate;
        } else {
          state.pending_hierarchy_action = hierarchy_action::remove;
        }
        state.pending_hierarchy_uuid = tree_request.uuid;
        break;
      case tree_action::create_prefab: {
        state.pending_prefab_author_action = prefab_author_action::create;
        state.pending_prefab_root_uuid = tree_request.uuid;
        state.prefab_path_buffer.fill('\0');
        const auto path = std::string{"prefabs/"} + tree_request.uuid.string() + ".prefab.json";
        const auto length = std::min(path.size(), state.prefab_path_buffer.size() - 1U);
        std::ranges::copy_n(path.begin(), length, state.prefab_path_buffer.begin());
        break;
      }
      case tree_action::reparent:
        (void)reparent_with_history(state, tree_request.uuid, tree_request.parent_uuid);
        break;
      }
      if (state.pending_prefab_author_action != prefab_author_action::none &&
          !ImGui::IsPopupOpen("Prefab Author Action")) {
        ImGui::OpenPopup("Prefab Author Action");
      }
      if (ImGui::BeginPopupModal("Prefab Author Action", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        switch (state.pending_prefab_author_action) {
        case prefab_author_action::create:
          ImGui::TextUnformatted(
              "Save a Prefab source and replace this subtree in the unsaved scene.");
          ImGui::TextDisabled("Undo restores the subtree but keeps the saved Prefab file.");
          ImGui::InputText("Asset path", state.prefab_path_buffer.data(),
                           state.prefab_path_buffer.size());
          break;
        case prefab_author_action::apply:
          ImGui::TextUnformatted(
              "Apply all component and property overrides to the shared source?");
          ImGui::TextDisabled(
              "Saves the shared source and clears undo history. The scene remains unsaved.");
          break;
        case prefab_author_action::unpack:
          ImGui::TextUnformatted("Unpack this instance into ordinary scene nodes?");
          ImGui::TextDisabled("The Prefab source and other instances will not be changed.");
          break;
        case prefab_author_action::none:
          break;
        }
        ImGui::BeginDisabled(state.prefab_author_busy);
        if (ImGui::Button("Confirm")) {
          state.prefab_author_busy = true;
          switch (state.pending_prefab_author_action) {
          case prefab_author_action::create:
            state.prefab_author_result =
                create_prefab_from_selected(state, state.pending_prefab_root_uuid,
                                            std::string_view{state.prefab_path_buffer.data()});
            break;
          case prefab_author_action::apply:
            state.prefab_author_result = apply_selected_prefab(
                state, state.pending_prefab_instance_uuid, state.pending_prefab_source_uuid);
            break;
          case prefab_author_action::unpack:
            state.prefab_author_result =
                unpack_selected_prefab(state, state.pending_prefab_instance_uuid);
            break;
          case prefab_author_action::none:
            state.prefab_author_result = gneiss::engine::error::invalid_state;
            break;
          }
          state.prefab_author_busy = false;
          state.prefab_author_attempted = !state.pending_file_operation;
          state.pending_prefab_author_action = prefab_author_action::none;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          state.pending_prefab_author_action = prefab_author_action::none;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
      }
    }
    ImGui::End();
    if (state.editing.prefabs().active() || state.pending_file_operation) {
      return submit_editor_ui(state);
    }

    if (state.history_error != gneiss::engine::error::success &&
        state.history_error != gneiss::engine::error::not_ready) {
      const auto message = state.history_error.message();
      ImGui::SetNextWindowPos(ImVec2(500.0F, 24.0F));
      ImGui::Begin("Command Error", nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration);
      ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                         static_cast<int>(message.size()), message.data());
      ImGui::End();
    }
    if (state.prefab_author_attempted &&
        state.prefab_author_result != gneiss::engine::error::success) {
      const auto message = state.prefab_author_result.message();
      ImGui::SetNextWindowPos(ImVec2(500.0F, 52.0F));
      ImGui::Begin("Prefab Author Error", nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration);
      ImGui::TextColored(gneiss::editor::theme_error_color(), "Prefab operation failed: %.*s",
                         static_cast<int>(message.size()), message.data());
      ImGui::End();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(400.0F, 280.0F), ImVec2(FLT_MAX, FLT_MAX));
    const auto scene_view_begun = state.panel_visibility.scene_view;
    const auto scene_view_visible =
        scene_view_begun && ImGui::Begin("Scene View", &state.panel_visibility.scene_view,
                                         ImGuiWindowFlags_NoBackground);
    if (!scene_view_visible && state.gizmo_drag.is_active()) {
      state.history_error =
          state.gizmo_drag.finish(state.editing.document(), state.editing.history());
      state.gizmo_wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      gneiss::editor::reset_transform_gizmo();
    }
    if (scene_view_visible) {
      const auto scene_view_hovered = ImGui::IsWindowHovered();
      ImGui::TextUnformatted("Scene View");
      if (ImGui::RadioButton("Move", state.gizmo_mode == gizmo_operation::translate)) {
        state.gizmo_mode = gizmo_operation::translate;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Rotate", state.gizmo_mode == gizmo_operation::rotate)) {
        state.gizmo_mode = gizmo_operation::rotate;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Scale", state.gizmo_mode == gizmo_operation::scale)) {
        state.gizmo_mode = gizmo_operation::scale;
      }
      ImGui::TextDisabled("RMB look | Wheel dolly | F focus selection");
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.95F, 0.35F, 0.35F, 1.0F), "X");
      ImGui::SameLine(0.0F, 3.0F);
      ImGui::TextColored(ImVec4(0.35F, 0.90F, 0.45F, 1.0F), "Y");
      ImGui::SameLine(0.0F, 3.0F);
      ImGui::TextColored(ImVec4(0.35F, 0.55F, 1.0F, 1.0F), "Z");
      if (const auto selected = state.editing.document().selected_node(); bool(selected)) {
        ImGui::TextColored(gneiss::editor::theme_warning_color(), "Selected: %s",
                           selected->display_name.c_str());
        const auto minimum = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRect(
            minimum, ImVec2(minimum.x + size.x, minimum.y + size.y),
            ImGui::ColorConvertFloat4ToU32(gneiss::editor::theme_warning_color()), 0.0F,
            ImDrawFlags_None, 2.0F);
      }
      const auto gizmo_minimum = ImGui::GetCursorScreenPos();
      const auto gizmo_size = ImGui::GetContentRegionAvail();
      const auto gizmo_owns_pointer = draw_transform_gizmo(state, gizmo_minimum, gizmo_size);
      draw_view_axis(state, gizmo_minimum, gizmo_size);
      if (scene_view_hovered && !gizmo_owns_pointer) {
        const auto camera_result = update_editor_camera(state, *time);
        if (camera_result != gneiss::engine::error::success) {
          ImGui::End();
          return camera_result;
        }
      }
    }
    if (scene_view_begun) {
      ImGui::End();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(260.0F, 220.0F), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::Begin("Inspector", &state.panel_visibility.inspector);
    ImGui::BeginDisabled(!state.editing.document().is_open());
    const auto save_button_pressed = ImGui::Button("Save");
    ImGui::EndDisabled();
    const auto save_requested =
        state.editing.document().is_open() &&
        (save_button_pressed ||
         (!material_focus && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
          ImGui::IsKeyPressed(ImGuiKey_S, false)));
    ImGui::SameLine();
    if (!state.editing.document().is_open()) {
      ImGui::TextDisabled("No scene");
    } else if (state.editing.document().is_dirty()) {
      ImGui::TextColored(gneiss::editor::theme_warning_color(), "Modified");
    } else {
      ImGui::TextColored(gneiss::editor::theme_success_color(), "Saved");
    }
    if (save_requested) {
      state.save_result = save_document(state);
      state.save_attempted = state.save_result != gneiss::engine::error::not_ready;
    }
    if (state.save_attempted && state.save_result != gneiss::engine::error::success) {
      const auto message = state.save_result.message();
      ImGui::TextColored(gneiss::editor::theme_error_color(), "Save failed: %.*s",
                         static_cast<int>(message.size()), message.data());
    }
    ImGui::Separator();
    if (const auto* runtime_selected = gneiss::editor::selected_runtime_node(
            state.runtime.scene_mirror(), state.runtime_selection);
        bool(runtime_selected)) {
      state.inspector_error = gneiss::engine::error::success;
      gneiss::editor::draw_runtime_inspector(
          *runtime_selected, make_runtime_inspector_actions(state, *runtime_selected));
    } else if (const auto prefab = state.editing.document().selected_prefab_node(); bool(prefab)) {
      state.inspector_error = gneiss::engine::error::success;
      ImGui::TextUnformatted(prefab->is_instance_root ? "Prefab Instance" : "Prefab Source Node");
      ImGui::Text("Name: %s", prefab->display_name.c_str());
      ImGui::Text("Instance UUID: %s", prefab->instance_uuid.string().c_str());
      if (!prefab->source_node_uuid.is_nil()) {
        ImGui::Text("Source UUID: %s", prefab->source_node_uuid.string().c_str());
      }
      ImGui::Text("Prefab: %s", prefab->prefab_uri.c_str());
      ImGui::Text("Entity: %llu", static_cast<unsigned long long>(prefab->entity.get()));
      {
        ImGui::BeginDisabled(!has_prefab_overrides(state, prefab->instance_uuid) ||
                             state.runtime.is_busy());
        if (ImGui::Button("Apply Overrides to Prefab...")) {
          state.pending_prefab_author_action = prefab_author_action::apply;
          state.pending_prefab_instance_uuid = prefab->instance_uuid;
          state.pending_prefab_source_uuid = prefab->source_node_uuid;
        }
        ImGui::EndDisabled();
      }
      ImGui::Separator();
      if (!prefab->is_instance_root) {
        const auto instance_uuid = prefab->instance_uuid;
        const auto source_uuid = prefab->source_node_uuid;
        const auto current =
            state.editing.document().find_prefab_source(instance_uuid, source_uuid);
        const bool has_override =
            bool(current) && current->has_overrides(gneiss::engine::transform_component_type);
        ImGui::BeginDisabled(!has_override);
        const bool restore_all = ImGui::Button("Restore All Transform");
        ImGui::EndDisabled();
        if (restore_all && current) {
          using namespace gneiss::engine;
          state.history_error =
              restore_prefab_component(state, instance_uuid, source_uuid, transform_component_type);
        }
      } else {
        const auto instance_uuid = prefab->instance_uuid;
        if (ImGui::Button("Rename Instance")) {
          state.rename_uuid = instance_uuid;
          state.rename_previous = prefab->display_name;
          state.rename_buffer.fill('\0');
          const auto length =
              std::min(state.rename_previous.size(), state.rename_buffer.size() - 1U);
          std::ranges::copy_n(state.rename_previous.begin(), length, state.rename_buffer.begin());
          ImGui::OpenPopup("Rename Prefab Instance");
        }
        if (ImGui::BeginPopup("Rename Prefab Instance")) {
          ImGui::InputText("Name", state.rename_buffer.data(), state.rename_buffer.size());
          if (ImGui::Button("Apply")) {
            const auto next = std::string{state.rename_buffer.data()};
            const auto current = state.editing.document().find_prefab_root(state.rename_uuid);
            state.history_error = !current
                                      ? gneiss::engine::error::not_found
                                      : state.editing.document().rename_node(current->node, next);
            if (state.history_error == gneiss::engine::error::success) {
              const auto uuid = state.rename_uuid;
              const auto previous = state.rename_previous;
              state.history_error = state.editing.history().record(
                  {.label = "重命名 Prefab 实例",
                   .undo =
                       [&state, uuid, previous] {
                         const auto node = state.editing.document().find_prefab_root(uuid);
                         return !node ? gneiss::engine::error::not_found
                                      : state.editing.document().rename_node(node->node, previous);
                       },
                   .redo =
                       [&state, uuid, next] {
                         const auto node = state.editing.document().find_prefab_root(uuid);
                         return !node ? gneiss::engine::error::not_found
                                      : state.editing.document().rename_node(node->node, next);
                       },
                   .merge_key = {}});
            }
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }
      }
    } else if (const auto selected = state.editing.document().selected_node(); bool(selected)) {
      ImGui::Text("Name: %s", selected->display_name.c_str());
      ImGui::Text("UUID: %s", selected->uuid.string().c_str());
      ImGui::Text("Entity: %llu", static_cast<unsigned long long>(selected->entity.get()));
      ImGui::Separator();
      const auto uuid = selected->uuid;
      const auto has_mesh =
          selected->components.get<gneiss::engine::mesh_renderer_description>() != nullptr;
      auto current = state.editing.document().components(selected->node);
      auto choices = gneiss::engine::component_editor_types();
      ImGui::BeginDisabled(state.editing.assets().active() || state.editing.history().is_pending());
      if (ImGui::Button("Components"))
        ImGui::OpenPopup("component_lifecycle");
      if (ImGui::BeginPopup("component_lifecycle")) {
        if (current && choices) {
          for (const auto& choice : *choices) {
            // 网格需要选择资产并保留资源引用，使用下面的异步资产入口。
            if (choice.route == gneiss::engine::component_editor_route::mesh_assets)
              continue;
            const bool exists = current->find(choice.type) != nullptr;
            if (exists ? !choice.removable : !choice.addable || !choice.removable)
              continue;
            auto type = gneiss::engine::meta::describe_type(choice.type);
            if (!type)
              continue;
            const auto label = std::string(exists ? "Remove " : "Add ") + std::string(type->name) +
                               "##" + choice.type.string();
            if (ImGui::MenuItem(label.c_str())) {
              state.history_error =
                  exists ? gneiss::editor::remove_component_description(
                               state.editing.document(), state.editing.history(), uuid, choice.type)
                         : gneiss::editor::add_component_description(state.editing.document(),
                                                                     state.editing.history(), uuid,
                                                                     choice.type);
              break;
            }
          }
        } else
          state.inspector_error = !current ? current.error() : choices.error();
        ImGui::EndPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!has_mesh);
      const auto remove_mesh_requested = ImGui::Button("Remove Mesh Renderer");
      ImGui::EndDisabled();
      if (remove_mesh_requested) {
        state.history_error = state.editing.assets().request_remove(state.editing.document(),
                                                                    state.editing.history(), uuid);
      }
      if (!has_mesh) {
        ImGui::TextDisabled("Select a mesh in Asset Browser to add Mesh Renderer");
      }
      ImGui::Separator();
    } else {
      state.inspector_error = gneiss::engine::error::success;
      ImGui::TextUnformatted("No node is selected");
    }
    if (!gneiss::editor::selected_runtime_node(state.runtime.scene_mirror(),
                                               state.runtime_selection))
      draw_component_descriptions(state);
    if (!gneiss::editor::selected_runtime_node(state.runtime.scene_mirror(),
                                               state.runtime_selection)) {
      draw_prefab_overrides(state);
      draw_prefab_conflict(state);
    }
    ImGui::End();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    draw_asset_browser(state);
#endif
    if (state.show_imgui_demo) {
      ImGui::ShowDemoWindow(&state.show_imgui_demo);
    }
    // 相机可能被本帧滚轮、聚焦或场景操作更新；网格必须使用最终相机状态。
    const auto grid_result = submit_editor_grid(state);
    if (grid_result != gneiss::engine::error::success) {
      return grid_result;
    }
    return submit_editor_ui(state);
  } catch (const std::bad_alloc&) {
    // C++ 异常不得越过 C ABI 回调边界。
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

uint8_t handle_close_requested(void* user_data) noexcept {
  if (!user_data) {
    return 1U;
  }
  auto& state = *static_cast<editor_state*>(user_data);
  if (state.closing)
    return 0U;
  state.material_editor.advance();
  if (state.material_editor.is_dirty() || state.material_editor.saving()) {
    state.material_editor_visible = true;
    state.material_edit_status = gneiss::engine::error::invalid_state;
    return 0U;
  }
  if (state.pending_file_operation) {
    state.document_actions.defer(gneiss::editor::document_action::exit_editor);
    return 0U;
  }
  if (state.scene_loader.active()) {
    (void)state.scene_loader.cancel(*state.application);
    begin_editor_exit(state);
    return 0U;
  }
  if (state.editing.active()) {
    state.editing.cancel(*state.application);
    state.document_actions.defer(gneiss::editor::document_action::exit_editor);
    return 0U;
  }
  state.editing.prefabs().dismiss();
  state.editing.assets().dismiss();
  state.scene_loader.dismiss();
  if (!state.editing.document().is_dirty()) {
    begin_editor_exit(state);
    return 0U;
  }
  state.document_actions.defer(gneiss::editor::document_action::exit_editor);
  return 0U;
}

int run_editor(int argc, char** argv) {
  launch_options options;
  if (!parse_options(argc, argv, options)) {
    std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=命令行解析，结果=%d，消息=参数无效\n",
                 gneiss::engine::error::invalid_argument.code());
    return 64;
  }
  gneiss::host::editor_project project;
  if (options.project.empty()) {
    const auto operation = gneiss::host::run_project_manager(options.smoke, project);
    if (operation == gneiss::engine::error::not_ready) {
      return 0;
    }
    if (operation != gneiss::engine::error::success) {
      const auto message = operation.message();
      std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=Project Manager，结果=%d，消息=%.*s\n",
                   operation.code(), static_cast<int>(message.size()), message.data());
      return 65;
    }
  } else {
    const auto operation = gneiss::host::load_editor_project(utf8_path(options.project), project);
    if (operation != gneiss::engine::error::success) {
      const auto message = operation.message();
      std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=工程加载，结果=%d，消息=%.*s，路径=%s\n",
                   operation.code(), static_cast<int>(message.size()), message.data(),
                   options.project.c_str());
      return 65;
    }
  }
  const auto asset_root_text = path_utf8(project.asset_root);
  if (asset_root_text.size() > std::numeric_limits<std::uint32_t>::max()) {
    report_startup_failure("资产根校验", gneiss::engine::error::invalid_argument, asset_root_text);
    return 64;
  }
  editor_state state(options.cooperative_tasks);
  std::shared_ptr<gneiss::runtime::application> application;
  state.smoke = options.smoke;
  state.smoke_frame_limit = options.smoke_frames;
  state.asset_root = project.asset_root;
  state.project_root = project.project_root;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  state.asset_reimports.start(state.project_root, state.asset_root);
  state.asset_reimports.request_refresh();
  start_source_asset_watch(state);
#endif
  const auto title = project.name + " - Gneiss Editor";
  // 工程启动引导仍负责准备原生配置目录；运行后的布局内容只走 user://。
  std::string user_root_text;
  if (!options.smoke) {
    const auto root = gneiss::host::default_editor_state_path().parent_path() / "layouts";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
      std::fprintf(stderr, "Gneiss Editor 用户目录创建失败：%s\n", error.message().c_str());
    user_root_text = path_utf8(root);
  }
  gneiss::runtime::application_configuration desc;
  desc.user_root = user_root_text;
  desc.callbacks.update = [&state](const gneiss::engine::frame_time& time) {
    return update_editor(&time, &state);
  };
  desc.callbacks.close_requested = [&state]() { return handle_close_requested(&state) != 0U; };
  desc.enable_window = true;
  desc.window.title = title;

  desc.window.width = 1280;
  desc.window.height = 720;
  desc.asset_root = asset_root_text;
  desc.writable_assets = true;

  desc.environment_asset = project.environment.asset;

  desc.environment_intensity = project.environment.intensity;
  desc.environment_rotation_radians =
      project.environment.rotation_degrees * 0.01745329251994329577F;

  auto operation = gneiss::runtime::application::create(desc, application);
  if (operation != gneiss::engine::error::success) {
    report_startup_failure("Editor Application 创建", operation, path_utf8(project.project_root));
    return 1;
  }
  operation = application->attach_task_executor(state.task_scheduler);
  if (operation != gneiss::engine::error::success) {
    return 2;
  }
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  const auto author_monitor_result = state.author_assets.initialize(application->async_files());
  if (author_monitor_result != gneiss::engine::error::success) {
    const auto message = author_monitor_result.message();
    std::fprintf(stderr, "Gneiss Editor 作者资产监视初始化失败：结果=%d，消息=%.*s\n",
                 author_monitor_result.code(), static_cast<int>(message.size()), message.data());
  } else {
    const auto author_watcher_result = state.author_asset_watcher.start(state.asset_root);
    state.author_watch_result = author_watcher_result;
    if (author_watcher_result != gneiss::engine::error::success) {
      const auto message = author_watcher_result.message();
      std::fprintf(stderr, "Gneiss Editor 作者资产监听启动失败：结果=%d，消息=%.*s\n",
                   author_watcher_result.code(), static_cast<int>(message.size()), message.data());
    }
  }
#endif
  state.application = application.get();
  operation = state.ui.initialize(*application);
  if (operation != gneiss::engine::error::success) {
    report_startup_failure("Editor UI 初始化", operation);
    return 2;
  }
  if (!options.smoke) {
    const auto layout_result = state.layout.initialize(
        application->async_files(), path_utf8(project.project_root), state.panel_visibility);
    if (layout_result != gneiss::engine::error::success &&
        layout_result != gneiss::engine::error::invalid_argument) {
      const auto message = layout_result.message();
      std::fprintf(stderr, "Gneiss Editor 布局加载失败：结果=%d，消息=%.*s\n", layout_result.code(),
                   static_cast<int>(message.size()), message.data());
    }
  }
  state.world = application->native_world();
  operation = state.world ? gneiss::engine::error::success : gneiss::engine::error::invalid_state;
  if (operation != gneiss::engine::error::success) {
    report_startup_failure("Editor World/Inspector 初始化", operation);
  } else {
    operation = state.editing.create_empty(*application, state.world);
    if (operation != gneiss::engine::error::success) {
      report_startup_failure("启动场景打开", operation, project.startup_scene);
    }
  }
  if (operation == gneiss::engine::error::success) {
    operation = state.camera.initialize();
    if (operation != gneiss::engine::error::success) {
      report_startup_failure("Editor Camera 初始化", operation);
    }
  }
  if (operation != gneiss::engine::error::success) {
    state.ui.shutdown(*application);
    (void)state.editing.close();
    return 3;
  }
  state.editing.history().clear();
  (void)state.scene_loader.request(*application, state.editing, project.startup_scene);
  const auto run_result = application->run(0U);
  state.pending_file_operation = {};
  begin_editor_exit(state);
  if (!options.smoke && !state.layout_save_started) {
    state.layout_save_started = true;
    state.layout_status = state.layout.save(state.panel_visibility);
  }
  auto editor_cleanup_pending = [&] {
    bool pending = state.editing.active() || state.scene_loader.active() || state.layout.saving() ||
                   state.runtime.is_running() || state.package_process.is_running();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    pending = pending || !state.author_assets.stopped() || !state.asset_reimports.status().stopped;
#endif
    return pending;
  };
  while (editor_cleanup_pending()) {
    state.runtime.update();
    state.package_process.update();
    (void)application->async_files()->dispatch_completions(64);
    bool scene_changed{};
    (void)state.scene_loader.advance(*application, state.editing, state.world, state.camera,
                                     scene_changed);
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    std::vector<std::filesystem::path> ignored;
    (void)state.author_assets.poll_rescan(ignored);
    (void)state.asset_reimports.poll_browser(state.assets, state.asset_result);
#endif
    if (state.task_scheduler.mode() == gneiss::engine::execution_mode::cooperative) {
      (void)state.task_scheduler.run_ready();
    }
    state.editing.advance_cleanup(*application);
    std::this_thread::yield();
  }
  if (!options.smoke) {
    const auto status =
        state.layout_status.failed() ? state.layout_status : state.layout.save_result();
    if (status.failed())
      std::fprintf(stderr, "Gneiss Editor 布局保存失败：%d\n", status.code());
  }
  state.ui.shutdown(*application);
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  if (state.asset_watcher.is_running()) {
    (void)state.asset_watcher.stop();
  }
  if (state.author_asset_watcher.is_running()) {
    (void)state.author_asset_watcher.stop();
  }
#endif
  state.editing.prefabs().dismiss();
  state.camera.shutdown();
  const auto closed = state.editing.close();
  auto shutdown_result = application->request_shutdown();
  if (shutdown_result.ok()) {
    do {
      if (state.task_scheduler.mode() == gneiss::engine::execution_mode::cooperative)
        (void)state.task_scheduler.run_ready();
      shutdown_result = application->advance_shutdown();
      if (shutdown_result == gneiss::engine::error::not_ready)
        std::this_thread::yield();
    } while (shutdown_result == gneiss::engine::error::not_ready);
  }
  if (shutdown_result.failed()) {
    report_startup_failure("Editor 异步关闭", shutdown_result);
    return 4;
  }
  if (closed.failed()) {
    report_startup_failure("Editor 文档关闭", closed);
    return 4;
  }
  if (run_result != gneiss::engine::error::success) {
    report_startup_failure("Editor 运行时事件循环", run_result, path_utf8(project.project_root));
  }
  return run_result == gneiss::engine::error::success ? 0 : 4;
}

} // namespace

namespace gneiss::host {

int run_editor_application(int argc, char** argv) noexcept {
  try {
    return run_editor(argc, argv);
  } catch (...) {
    return 99;
  }
}

} // namespace gneiss::host
