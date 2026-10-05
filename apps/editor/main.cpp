// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_scene_commands.hpp"
#include "author_hierarchy_panel.hpp"
#include "author_property_edit.hpp"
#include "author_property_panel.hpp"
#include "child_process.hpp"
#include "console_panel.hpp"
#include "editor_camera.hpp"
#include "editor_command_history.hpp"
#include "editor_grid.hpp"
#include "editor_project.hpp"
#include "editor_rotation_math.hpp"
#include "editor_session.hpp"
#include "editor_theme.hpp"
#include "editor_ui.hpp"
#include "engine/function/application/application_asset_reload_internal.hpp"
#include "imgui_adapter.hpp"
#include "native_author_transaction.hpp"
#include "native_dialog.hpp"
#include "prefab_authoring.hpp"
#include "project_manager.hpp"
#include "project_workspace.hpp"
#include "property_inspector_model.hpp"
#include "runtime_author_apply.hpp"
#include "runtime_launch.hpp"
#include "runtime_panels.hpp"
#include "runtime_process.hpp"
#include "transform_gizmo_drag.hpp"
#include "transform_gizmo_math.hpp"
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
#include "asset_background_worker.hpp"
#include "asset_browser_model.hpp"
#include "asset_browser_panel.hpp"
#include "asset_file_watcher.hpp"
#include "asset_import_controller.hpp"
#include "author_asset_service.hpp"
#endif

#include <gneiss/application.hpp>

#include <ImGuizmo.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class hierarchy_action { none, rename, duplicate, remove };
enum class document_action { none, new_scene, open_scene, exit_editor };
enum class gizmo_operation { translate, rotate, scale };
enum class author_selection_kind : std::uint8_t { none, scene_node, prefab_root, prefab_source };
enum class prefab_author_action : std::uint8_t { none, create, apply, unpack };

struct author_selection final {
  author_selection_kind kind = author_selection_kind::none;
  std::string primary_uuid;
  std::string source_uuid;
};

struct author_asset_document final {
  std::string relative_path;
  std::string content;
};

struct author_scene_document final {
  std::string relative_path;
  std::string content;
};

struct author_document_operation final {
  const std::vector<gneiss::editor::author_document_change>* changes = nullptr;
  const std::vector<gneiss::editor::author_document_change>* rollback = nullptr;
};

struct prefab_refresh_guard final {
  gneiss::editor::editor_session* session = nullptr;
  std::vector<gneiss_scene_prefab_refresh_token> tokens;

  ~prefab_refresh_guard() noexcept {
    if (session != nullptr) {
      for (const auto token : tokens) {
        session->release_prefab_refresh(token);
      }
    }
  }
};

gneiss::result
toggle_prefab_refreshes(gneiss::editor::editor_session& session,
                        const std::vector<gneiss_scene_prefab_refresh_token>& tokens) noexcept {
  std::size_t toggled_count = 0U;
  for (const auto token : tokens) {
    gneiss::scene_node_id root;
    const auto operation = session.toggle_prefab_refresh(token, root);
    if (operation != gneiss::result::success) {
      while (toggled_count > 0U) {
        --toggled_count;
        (void)session.toggle_prefab_refresh(tokens[toggled_count], root);
      }
      return operation;
    }
    ++toggled_count;
  }
  return gneiss::result::success;
}

struct editor_state {
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  explicit editor_state(bool cooperative = false)
      : task_scheduler({.mode = cooperative ? gneiss::tasks::execution_mode::cooperative
                                            : gneiss::tasks::execution_mode::thread_pool}) {}
#else
  explicit editor_state(bool = false) {}
#endif
  gneiss::editor::imgui_adapter ui;
  gneiss::editor::editor_camera camera;
  gneiss::editor::editor_session session;
  gneiss::editor::editor_command_history history;
  gneiss::editor::property_inspector_model inspector;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::entity_id inspected_entity;
  gneiss::editor::runtime_scene_selection runtime_selection;
  gneiss::result inspector_error = gneiss::result::success;
  gneiss::result history_error = gneiss::result::success;
  gneiss::result prefab_author_result = gneiss::result::success;
  bool prefab_author_attempted = false;
  bool prefab_author_busy = false;
  std::array<char, 192> prefab_path_buffer{};
  prefab_author_action pending_prefab_author_action = prefab_author_action::none;
  std::string pending_prefab_instance_uuid;
  std::string pending_prefab_source_uuid;
  std::string pending_prefab_root_uuid;
  std::filesystem::path asset_root;
  std::filesystem::path project_root;
  gneiss::result save_result = gneiss::result::success;
  bool save_attempted = false;
  gneiss::editor::runtime_process runtime;
  gneiss::result runtime_result = gneiss::result::success;
  bool runtime_attempted = false;
  gneiss::child_process package_process;
  gneiss::result package_result = gneiss::result::success;
  bool package_attempted = false;
  bool show_package_dialog = false;
  bool package_zip = true;
  int package_profile = 1;
  std::array<char, 512> package_output{};
  gneiss::editor::console_panel_state console_panel;
  bool pending_save_and_run = false;
  bool show_imgui_demo = false;
  gneiss::editor::editor_panel_visibility panel_visibility;
  gizmo_operation gizmo_mode = gizmo_operation::translate;
  gneiss::editor::transform_gizmo_drag gizmo_drag;
  bool gizmo_wait_release = false;
  std::uint64_t property_edit_serial = 0U;
  std::array<char, 128> rename_buffer{};
  std::string rename_uuid;
  std::string rename_previous;
  hierarchy_action pending_hierarchy_action = hierarchy_action::none;
  std::string pending_hierarchy_uuid;
  document_action pending_document_action = document_action::none;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  gneiss::editor::asset_browser_model assets;
  gneiss::editor::asset_file_watcher asset_watcher;
  std::chrono::steady_clock::time_point next_asset_watch_attempt{};
  bool asset_watch_failed = false;
  gneiss::result asset_watch_result = gneiss::result::success;
  gneiss::result author_watch_result = gneiss::result::success;
  gneiss::editor::asset_file_watcher author_asset_watcher;
  gneiss::tasks::task_scheduler task_scheduler;
  gneiss::editor::author_asset_service author_assets{task_scheduler};
  gneiss::editor::asset_background_worker asset_reimports{{}, &task_scheduler};
  bool asset_shutdown_pending{};
  std::size_t observed_author_drops = 0U;
  std::size_t observed_source_drops = 0U;
  std::size_t observed_candidate_drops = 0U;
  gneiss::editor::asset_browser_result asset_result = gneiss::editor::asset_browser_result::success;
  ImGuiTextFilter asset_filter;
  gneiss::editor::editor_import_report last_import;
  bool import_attempted = false;
  gneiss::result asset_scene_result = gneiss::result::success;
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
  if (operation == gneiss::result::not_ready) {
    return;
  }
  if (operation == gneiss::result::success) {
    state.asset_reimports.request_rescan();
    state.asset_reimports.request_refresh();
    return;
  }
  state.asset_watch_failed = true;
  const auto message = operation.message();
  std::fprintf(stderr, "Gneiss Editor 资产监听启动失败：结果=%d，消息=%.*s\n",
               gneiss::to_native(operation), static_cast<int>(message.size()), message.data());
}
#endif

constexpr std::size_t matrix_index(std::size_t row, std::size_t column) noexcept {
  return (column * 4U) + row;
}

gneiss::result submit_editor_grid(gneiss_application application, const editor_state& state) {
  const auto* viewport = ImGui::GetMainViewport();
  const auto& camera = state.camera.current_transform();
  auto lines =
      gneiss::editor::build_editor_grid(camera, gneiss::editor::build_gizmo_view_matrix(camera),
                                        viewport != nullptr ? viewport->Size.y : 0.0F);
  lines.push_back({.start = {0.0F, 0.0F, 0.0F},
                   .end = {2.0F, 0.0F, 0.0F},
                   .color_rgba8 = IM_COL32(243, 139, 168, 255),
                   .width = 2.0F,
                   .depth_test = 1U,
                   .reserved = {}});
  lines.push_back({.start = {0.0F, 0.0F, 0.0F},
                   .end = {0.0F, 2.0F, 0.0F},
                   .color_rgba8 = IM_COL32(166, 227, 161, 255),
                   .width = 2.0F,
                   .depth_test = 1U,
                   .reserved = {}});
  lines.push_back({.start = {0.0F, 0.0F, 0.0F},
                   .end = {0.0F, 0.0F, 2.0F},
                   .color_rgba8 = IM_COL32(137, 180, 250, 255),
                   .width = 2.0F,
                   .depth_test = 1U,
                   .reserved = {}});
  gneiss::debug_draw_list_desc desc = GNEISS_DEBUG_DRAW_LIST_DESC_INIT;
  desc.line_count = static_cast<std::uint32_t>(lines.size());
  desc.lines = lines.data();
  return gneiss::from_native(gneiss_application_submit_debug_draw_list(application, &desc));
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
  bool cooperative_tasks = false;
  std::string project;
};

bool parse_options(int argc, char** argv, launch_options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--smoke") {
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

void report_startup_failure(std::string_view stage, gneiss::result operation,
                            std::string_view path = {}) noexcept {
  const auto message = operation.message();
  std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=%.*s，结果=%d，消息=%.*s",
               static_cast<int>(stage.size()), stage.data(), gneiss::to_native(operation),
               static_cast<int>(message.size()), message.data());
  if (!path.empty()) {
    std::fprintf(stderr, "，路径=%.*s", static_cast<int>(path.size()), path.data());
  }
  std::fputc('\n', stderr);
}

void synchronize_history_dirty(editor_state& state) noexcept {
  if (state.history.is_dirty()) {
    state.session.mark_dirty();
  } else {
    state.session.clear_dirty();
  }
}

gneiss::result undo_editor_command(editor_state& state) noexcept {
  const auto result = state.history.undo();
  if (result == gneiss::result::success) {
    if (!state.session.is_dirty()) {
      state.history.mark_saved();
    }
    synchronize_history_dirty(state);
  }
  return result;
}

gneiss::result redo_editor_command(editor_state& state) noexcept {
  const auto result = state.history.redo();
  if (result == gneiss::result::success) {
    if (!state.session.is_dirty()) {
      state.history.mark_saved();
    }
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

[[nodiscard]] gneiss::result author_uri_path(const editor_state& state, std::string_view uri,
                                             std::filesystem::path& path,
                                             std::string& relative_path) noexcept {
  constexpr std::string_view prefix = "asset://";
  if (!uri.starts_with(prefix) || uri.size() == prefix.size()) {
    return gneiss::result::invalid_argument;
  }
  try {
    relative_path.assign(uri.substr(prefix.size()));
    path = state.asset_root / utf8_path(relative_path);
    return gneiss::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::result::out_of_memory;
  } catch (...) {
    return gneiss::result::io;
  }
}

[[nodiscard]] gneiss::result read_author_asset(const editor_state& state, std::string_view uri,
                                               author_asset_document& output) noexcept {
  std::filesystem::path path;
  auto operation = author_uri_path(state, uri, path, output.relative_path);
  if (operation != gneiss::result::success) {
    return operation;
  }
  try {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
      return gneiss::result::not_found;
    }
    output.content.assign(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
    return stream.bad() ? gneiss::result::io : gneiss::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::result::out_of_memory;
  } catch (...) {
    return gneiss::result::io;
  }
}

void restore_author_selection(editor_state& state, const author_selection& selection) noexcept {
  const gneiss::editor::scene_node_record* scene_node = nullptr;
  const gneiss::editor::prefab_node_record* prefab_node = nullptr;
  switch (selection.kind) {
  case author_selection_kind::scene_node:
    scene_node = state.session.find_node(selection.primary_uuid);
    break;
  case author_selection_kind::prefab_root:
    prefab_node = state.session.find_prefab_root(selection.primary_uuid);
    break;
  case author_selection_kind::prefab_source:
    prefab_node = state.session.find_prefab_source(selection.primary_uuid, selection.source_uuid);
    break;
  case author_selection_kind::none:
    break;
  }
  if (scene_node != nullptr) {
    (void)state.session.select(scene_node->node);
  } else if (prefab_node != nullptr) {
    (void)state.session.select(prefab_node->node);
  }
}

[[nodiscard]] gneiss::result apply_author_documents(editor_state& state,
                                                    gneiss_application application,
                                                    author_document_operation documents,
                                                    const author_selection& selection) noexcept {
  if (documents.changes == nullptr || documents.rollback == nullptr) {
    return gneiss::result::invalid_argument;
  }
  try {
    auto operation =
        gneiss::editor::commit_native_author_transaction(state.asset_root, *documents.changes);
    if (operation == gneiss::result::success) {
      const auto uri = std::string{state.session.uri()};
      operation = state.session.open(application, state.world, uri);
      if (operation != gneiss::result::success) {
        (void)gneiss::editor::commit_native_author_transaction(state.asset_root,
                                                               *documents.rollback);
        (void)state.session.open(application, state.world, uri);
      }
    }
    if (operation == gneiss::result::success) {
      restore_author_selection(state, selection);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return gneiss::result::out_of_memory;
  } catch (...) {
    return gneiss::result::internal;
  }
}

[[nodiscard]] gneiss::result
record_author_documents(editor_state& state, gneiss_application application, std::string label,
                        std::vector<gneiss::editor::author_document_change> changes,
                        const author_selection& undo_selection,
                        const author_selection& redo_selection) {
  std::vector<gneiss::editor::author_document_change> inverse;
  auto operation = gneiss::editor::invert_author_document_changes(changes, inverse);
  if (operation != gneiss::result::success) {
    return operation;
  }
  try {
    auto forward =
        std::make_shared<std::vector<gneiss::editor::author_document_change>>(std::move(changes));
    auto backward =
        std::make_shared<std::vector<gneiss::editor::author_document_change>>(std::move(inverse));
    auto undo_target = std::make_shared<author_selection>(undo_selection);
    auto redo_target = std::make_shared<author_selection>(redo_selection);
    operation = apply_author_documents(
        state, application, {.changes = forward.get(), .rollback = backward.get()}, redo_selection);
    if (operation != gneiss::result::success) {
      return operation;
    }
    operation = state.history.record(
        {.label = std::move(label),
         .undo =
             [&state, application, forward, backward, undo_target] {
               return apply_author_documents(state, application,
                                             {.changes = backward.get(), .rollback = forward.get()},
                                             *undo_target);
             },
         .redo =
             [&state, application, forward, backward, redo_target] {
               return apply_author_documents(state, application,
                                             {.changes = forward.get(), .rollback = backward.get()},
                                             *redo_target);
             },
         .merge_key = {}});
    if (operation != gneiss::result::success) {
      (void)apply_author_documents(state, application,
                                   {.changes = backward.get(), .rollback = forward.get()},
                                   undo_selection);
      return operation;
    }
    state.history.mark_saved();
    return gneiss::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::result::out_of_memory;
  } catch (...) {
    return gneiss::result::internal;
  }
}

[[nodiscard]] gneiss::result current_author_scene(editor_state& state,
                                                  author_scene_document& output) noexcept {
  author_asset_document document;
  const auto operation = read_author_asset(state, state.session.uri(), document);
  output.relative_path = std::move(document.relative_path);
  output.content = std::move(document.content);
  return operation;
}

[[nodiscard]] gneiss::result apply_selected_prefab(editor_state& state,
                                                   gneiss_application application,
                                                   std::string_view instance_uuid,
                                                   std::string_view source_uuid) {
  const auto* selected = state.session.find_prefab_source(instance_uuid, source_uuid);
  if (selected == nullptr || selected->override_flags == 0U || state.session.is_dirty() ||
      state.runtime.is_busy()) {
    return gneiss::result::invalid_state;
  }
  author_scene_document scene;
  auto operation = current_author_scene(state, scene);
  author_asset_document prefab;
  if (operation == gneiss::result::success) {
    operation = read_author_asset(state, selected->prefab_uri, prefab);
  }
  gneiss::editor::apply_prefab_author_plan plan;
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::prepare_apply_prefab(scene.content, prefab.content,
                                                     {.scene_path = scene.relative_path,
                                                      .prefab_path = prefab.relative_path,
                                                      .prefab_uri = selected->prefab_uri,
                                                      .instance_uuid = instance_uuid},
                                                     plan);
  }
  if (operation == gneiss::result::success) {
    const author_selection selection{.kind = author_selection_kind::prefab_source,
                                     .primary_uuid = std::string{instance_uuid},
                                     .source_uuid = std::string{source_uuid}};
    operation = record_author_documents(state, application, "应用 Prefab 实例覆盖",
                                        std::move(plan.changes), selection, selection);
  }
  return operation;
}

[[nodiscard]] gneiss::result unpack_selected_prefab(editor_state& state,
                                                    gneiss_application application,
                                                    std::string_view instance_uuid) {
  const auto* root = state.session.find_prefab_root(instance_uuid);
  if (root == nullptr || state.session.is_dirty() || state.runtime.is_busy()) {
    return gneiss::result::invalid_state;
  }
  const auto prefab_uri = root->prefab_uri;
  author_scene_document scene;
  auto operation = current_author_scene(state, scene);
  author_asset_document prefab;
  if (operation == gneiss::result::success) {
    operation = read_author_asset(state, prefab_uri, prefab);
  }
  std::string unpacked_root_uuid;
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::make_editor_uuid(unpacked_root_uuid);
  }
  std::vector<std::string> target_uuids;
  std::vector<gneiss::editor::unpack_prefab_uuid_mapping> mappings;
  if (operation == gneiss::result::success) {
    const auto count = std::ranges::count_if(state.session.prefab_nodes(), [&](const auto& node) {
      return !node.is_instance_root && node.instance_uuid == instance_uuid;
    });
    target_uuids.reserve(static_cast<std::size_t>(count));
    mappings.reserve(static_cast<std::size_t>(count));
    for (const auto& node : state.session.prefab_nodes()) {
      if (!node.is_instance_root && node.instance_uuid == instance_uuid) {
        target_uuids.emplace_back();
        operation = gneiss::editor::make_editor_uuid(target_uuids.back());
        if (operation != gneiss::result::success) {
          break;
        }
        mappings.push_back(
            {.source_node_uuid = node.source_node_uuid, .target_node_uuid = target_uuids.back()});
      }
    }
  }
  std::vector<gneiss::editor::author_document_change> changes;
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::prepare_unpack_prefab(scene.content, prefab.content,
                                                      {.scene_path = scene.relative_path,
                                                       .prefab_uri = prefab_uri,
                                                       .instance_uuid = instance_uuid,
                                                       .instance_root_uuid = unpacked_root_uuid,
                                                       .node_mappings = mappings},
                                                      changes);
  }
  if (operation == gneiss::result::success) {
    operation = record_author_documents(state, application, "解包 Prefab 实例", std::move(changes),
                                        {.kind = author_selection_kind::prefab_root,
                                         .primary_uuid = std::string{instance_uuid},
                                         .source_uuid = {}},
                                        {.kind = author_selection_kind::scene_node,
                                         .primary_uuid = unpacked_root_uuid,
                                         .source_uuid = {}});
  }
  return operation;
}

[[nodiscard]] gneiss::result create_prefab_from_selected(editor_state& state,
                                                         gneiss_application application,
                                                         std::string_view root_uuid,
                                                         std::string_view prefab_path) {
  if (state.session.find_node(root_uuid) == nullptr || state.session.is_dirty() ||
      state.runtime.is_busy() || prefab_path.empty()) {
    return gneiss::result::invalid_state;
  }
  author_scene_document scene;
  auto operation = current_author_scene(state, scene);
  std::string prefab_uri = "asset://";
  prefab_uri.append(prefab_path);
  std::string prefab_uuid;
  std::string instance_uuid;
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::make_editor_uuid(prefab_uuid);
  }
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::make_editor_uuid(instance_uuid);
  }
  std::vector<gneiss::editor::author_document_change> changes;
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::prepare_create_prefab(scene.content,
                                                      {.scene_path = scene.relative_path,
                                                       .prefab_path = prefab_path,
                                                       .prefab_uri = prefab_uri,
                                                       .root_uuid = root_uuid,
                                                       .prefab_uuid = prefab_uuid,
                                                       .instance_uuid = instance_uuid},
                                                      changes);
  }
  if (operation == gneiss::result::success) {
    operation =
        record_author_documents(state, application, "从场景子树创建 Prefab", std::move(changes),
                                {.kind = author_selection_kind::scene_node,
                                 .primary_uuid = std::string{root_uuid},
                                 .source_uuid = {}},
                                {.kind = author_selection_kind::prefab_root,
                                 .primary_uuid = instance_uuid,
                                 .source_uuid = {}});
  }
  return operation;
}

gneiss::result save_document_as(editor_state& state) {
  std::filesystem::path path;
  auto operation = gneiss::editor::select_scene_save_path(path);
  if (operation != gneiss::result::success) {
    return operation;
  }
  std::string uri;
  std::string saved_content;
  operation = gneiss::editor::make_asset_uri(state.asset_root, path, uri);
  if (operation == gneiss::result::success) {
    operation = state.session.save_as(state.asset_root, uri, &saved_content);
  }
  if (operation == gneiss::result::success) {
    state.history.mark_saved();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    const auto saved_uri = std::string{state.session.uri()};
    const auto acknowledged = state.author_assets.acknowledge(saved_uri, std::move(saved_content));
    if (acknowledged != gneiss::result::success) {
      state.author_assets.mark_failed(saved_uri, acknowledged);
    }
    (void)state.runtime.publish_asset_revision(std::span<const std::string>(&saved_uri, 1U));
#endif
  }
  return operation;
}

gneiss::result save_document(editor_state& state) {
  if (state.session.uri().empty()) {
    return save_document_as(state);
  }
  std::string saved_content;
  const auto operation = state.session.save(state.asset_root, &saved_content);
  if (operation == gneiss::result::success) {
    state.history.mark_saved();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    const auto uri = std::string{state.session.uri()};
    const auto acknowledged = state.author_assets.acknowledge(uri, std::move(saved_content));
    if (acknowledged != gneiss::result::success) {
      state.author_assets.mark_failed(uri, acknowledged);
    }
    (void)state.runtime.publish_asset_revision(std::span<const std::string>(&uri, 1U));
#endif
  }
  return operation;
}

gneiss::result launch_runtime(editor_state& state, bool save_changes) noexcept {
#if defined(GNEISS_EDITOR_HAS_RUNTIME)
  gneiss::editor::runtime_launch_request request;
  auto operation = save_changes ? save_document(state) : gneiss::result::success;
  if (operation == gneiss::result::success &&
      gneiss::editor::inspect_runtime_launch(state.session, state.project_root, request) !=
          gneiss::editor::runtime_launch_state::ready) {
    operation = gneiss::result::invalid_state;
  }
  if (operation == gneiss::result::success && save_changes) {
    state.history.mark_saved();
  }
  if (operation == gneiss::result::success) {
    gneiss::app::project_description project;
    operation = gneiss::app::load_project_description(state.project_root, project);
    if (operation == gneiss::result::success && project.game_module.name.empty()) {
      operation = state.runtime.start(std::filesystem::path{GNEISS_EDITOR_RUNTIME_PATH}, request);
    } else if (operation == gneiss::result::success) {
      operation = state.runtime.build_and_start(std::filesystem::path{GNEISS_EDITOR_CMAKE_PATH},
                                                std::filesystem::path{GNEISS_EDITOR_RUNTIME_PATH},
                                                request, project);
    }
  }
  return operation;
#else
  (void)state;
  (void)save_changes;
  return gneiss::result::unsupported;
#endif
}

void request_runtime_launch(editor_state& state) noexcept {
  gneiss::editor::runtime_launch_request request;
  const auto launch_state =
      gneiss::editor::inspect_runtime_launch(state.session, state.project_root, request);
  if (launch_state == gneiss::editor::runtime_launch_state::requires_save) {
    state.pending_save_and_run = true;
    ImGui::OpenPopup("Save and Run");
    return;
  }
  state.runtime_result = launch_runtime(state, false);
  state.runtime_attempted = true;
}

gneiss::result perform_document_action(editor_state& state, gneiss_application application,
                                       document_action action) {
  gneiss::result operation = gneiss::result::success;
  switch (action) {
  case document_action::new_scene:
    operation = state.session.create_empty(application, state.world);
    break;
  case document_action::open_scene: {
    std::filesystem::path path;
    operation = gneiss::editor::select_scene_file(path);
    std::string uri;
    if (operation == gneiss::result::success) {
      operation = gneiss::editor::make_asset_uri(state.asset_root, path, uri);
    }
    if (operation == gneiss::result::success) {
      operation = state.session.open(application, state.world, uri);
    }
    break;
  }
  case document_action::exit_editor:
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    state.asset_shutdown_pending = true;
    state.asset_reimports.request_stop();
    state.author_assets.request_stop();
#else
    operation = gneiss::from_native(gneiss_application_request_exit(application));
#endif
    break;
  case document_action::none:
    return gneiss::result::success;
  }
  if (operation == gneiss::result::success && action != document_action::exit_editor) {
    state.history.clear();
    state.inspector.clear();
    state.inspected_entity = {};
  }
  return operation;
}

void request_document_action(editor_state& state, gneiss_application application,
                             document_action action) {
  if (state.session.is_dirty()) {
    state.pending_document_action = action;
    ImGui::OpenPopup("Unsaved Changes");
  } else {
    state.history_error = perform_document_action(state, application, action);
  }
}

bool reparent_with_history(editor_state& state, std::string_view source_uuid,
                           std::string_view target_uuid) {
  const auto* source = state.session.find_node(source_uuid);
  const auto* target = target_uuid.empty() ? nullptr : state.session.find_node(target_uuid);
  if (source == nullptr || (!target_uuid.empty() && target == nullptr) ||
      (target != nullptr && source->node == target->node)) {
    return false;
  }
  std::string previous_parent_uuid;
  if (source->parent.is_valid()) {
    const auto parent = std::ranges::find(state.session.nodes(), source->parent,
                                          &gneiss::editor::scene_node_record::node);
    if (parent != state.session.nodes().end()) {
      previous_parent_uuid = parent->uuid;
    }
  }
  if (previous_parent_uuid == target_uuid) {
    return false;
  }
  const std::string source_key{source_uuid};
  const std::string target_key{target_uuid};
  state.history_error = state.session.reparent_node(
      source->node, target == nullptr ? gneiss::scene_node_id{} : target->node);
  if (state.history_error != gneiss::result::success) {
    return false;
  }
  state.history_error = state.history.record(
      {.label = "移动节点",
       .undo =
           [&state, source_key, previous_parent_uuid] {
             const auto* current = state.session.find_node(source_key);
             const auto* parent = previous_parent_uuid.empty()
                                      ? nullptr
                                      : state.session.find_node(previous_parent_uuid);
             return current == nullptr
                        ? gneiss::result::not_found
                        : state.session.reparent_node(current->node, parent == nullptr
                                                                         ? gneiss::scene_node_id{}
                                                                         : parent->node);
           },
       .redo =
           [&state, source_key, target_key] {
             const auto* current = state.session.find_node(source_key);
             const auto* parent =
                 target_key.empty() ? nullptr : state.session.find_node(target_key);
             return current == nullptr || (!target_key.empty() && parent == nullptr)
                        ? gneiss::result::not_found
                        : state.session.reparent_node(current->node, parent == nullptr
                                                                         ? gneiss::scene_node_id{}
                                                                         : parent->node);
           },
       .merge_key = {}});
  if (state.history_error != gneiss::result::success) {
    const auto* current = state.session.find_node(source_key);
    const auto* parent =
        previous_parent_uuid.empty() ? nullptr : state.session.find_node(previous_parent_uuid);
    if (current != nullptr) {
      (void)state.session.reparent_node(current->node,
                                        parent == nullptr ? gneiss::scene_node_id{} : parent->node);
    }
  }
  return true;
}

gneiss::editor::runtime_inspector_actions
runtime_inspector_actions(editor_state& state, const gneiss::editor::runtime_scene_node& node) {
  return {.context = &state,
          .editable = state.runtime.supports_property_editing(),
          .can_apply_to_author =
              !node.uuid.empty() && state.session.find_node(node.uuid) != nullptr,
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
                    editor.session, editor.history,
                    {selected.uuid, selected.prefab_instance_uuid, selected.prefab_source_node_uuid,
                     selected.local_transform});
                if (editor.history_error == gneiss::result::success) {
                  synchronize_history_dirty(editor);
                }
              },
          .report_result =
              [](void* context, gneiss::result status) {
                auto& editor = *static_cast<editor_state*>(context);
                editor.runtime_result = status;
                editor.runtime_attempted = true;
              }};
}

void draw_reflected_properties(editor_state& state) {
  gneiss::editor::draw_author_properties(
      state.inspector.components(), state.property_edit_serial, state.inspector_error,
      {
          .context = &state,
          .write =
              [](void* context, const gneiss::editor::inspector_component& component,
                 const gneiss::editor::inspector_property& property,
                 const gneiss_property_value& value, std::uint64_t serial) {
                auto& editor = *static_cast<editor_state*>(context);
                return gneiss::editor::edit_author_property(editor.session, editor.history,
                                                            editor.inspector, editor.world,
                                                            component, property, value, serial);
              },
      });
}

#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
void execute_asset_browser_request(editor_state& state,
                                   const gneiss::editor::asset_browser_request& request) {
  using namespace gneiss::editor;
  using action = asset_browser_action;
  const auto entry =
      std::ranges::find(state.assets.entries(), request.asset_id, &asset_browser_entry::id);
  const auto queued = [&state](bool accepted) {
    if (!accepted) {
      state.last_import = {};
      state.last_import.diagnostic = "导入队列已满或正在关闭，请重试";
      state.import_attempted = true;
    }
  };
  switch (request.action) {
  case action::none:
    break;
  case action::restart_source_watch: {
    const auto stopped =
        state.asset_watcher.is_running() ? state.asset_watcher.stop() : gneiss::result::success;
    state.asset_watch_result =
        stopped.ok() ? state.asset_watcher.start(state.project_root / "sources", true) : stopped;
    if (state.asset_watch_result.ok()) {
      state.asset_reimports.request_rescan();
    }
    state.asset_watch_failed = state.asset_watch_result != gneiss::result::success &&
                               state.asset_watch_result != gneiss::result::not_ready;
    break;
  }
  case action::restart_author_watch: {
    const auto stopped = state.author_asset_watcher.is_running() ? state.author_asset_watcher.stop()
                                                                 : gneiss::result::success;
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
    } else if (selected_result != gneiss::result::not_ready) {
      state.last_import = {};
      state.last_import.result = editor_import_result::io_error;
      state.last_import.diagnostic = std::string{selected_result.message()};
      state.import_attempted = true;
    }
    break;
  }
  case action::reimport_selected:
    if (entry != state.assets.entries().end() && entry->kind == asset_browser_kind::source) {
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
      if (material != nullptr) {
        state.asset_scene_result =
            add_mesh_asset(state.session, state.history, entry->display_name,
                           {.mesh_uri = entry->asset_uri, .material_uri = material->asset_uri});
        state.asset_scene_attempted = true;
      }
    }
    break;
  case action::add_prefab:
    if (entry != state.assets.entries().end() && is_prefab_asset(*entry)) {
      const auto* node = state.session.selected_node();
      state.asset_scene_result =
          add_prefab_asset(state.session, state.history, entry->display_name, entry->asset_uri,
                           node == nullptr ? gneiss::scene_node_id{} : node->node);
      state.asset_scene_attempted = true;
    }
    break;
  case action::apply_to_node: {
    const auto* node = state.session.selected_node();
    if (node != nullptr && entry != state.assets.entries().end()) {
      const auto mesh = is_mesh_asset(*entry) && !node->material_uri.empty();
      const auto material = is_material_asset(*entry) && !node->mesh_uri.empty();
      if (mesh || material) {
        state.asset_scene_result =
            replace_mesh_assets(state.session, state.history, node->uuid,
                                {.mesh_uri = mesh ? entry->asset_uri : node->mesh_uri,
                                 .material_uri = material ? entry->asset_uri : node->material_uri});
        state.asset_scene_attempted = true;
      }
    }
    break;
  }
  }
}

void draw_asset_browser(editor_state& state) {
  using namespace gneiss::editor;
  const auto background = state.asset_reimports.status();
  const auto source_root = path_utf8(state.project_root / "sources");
  const auto author_root = path_utf8(state.asset_root);
  const auto worker_source = path_utf8(background.source);
  asset_browser_view view;
  view.watches[0] = {.root = source_root,
                     .operation = state.asset_watch_result,
                     .dropped = state.asset_watcher.dropped_event_count(),
                     .waiting = state.asset_watch_result == gneiss::result::not_ready &&
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
  view.refresh_failed = state.asset_result != asset_browser_result::success;
  view.import_attempted = state.import_attempted;
  view.import_succeeded = state.last_import.result == editor_import_result::success;
  view.can_retry_import =
      state.import_attempted && !view.import_succeeded && !state.last_import.source_path.empty();
  view.import_diagnostic = state.last_import.diagnostic;
  const auto selected =
      std::ranges::find(state.assets.entries(), state.assets.selection(), &asset_browser_entry::id);
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
  view.scene.can_retry = load.phase == gneiss::ipc_scene_phase::failed ||
                         load.phase == gneiss::ipc_scene_phase::cancelled;
  view.scene.can_load = view.scene.enabled && selected != state.assets.entries().end() &&
                        selected->asset_uri.ends_with(".scene.json") &&
                        (!view.scene.visible || gneiss::scene_phase_terminal(load.phase));
  if (load.budget) {
    const auto& budget = *load.budget;
    view.scene.budget = asset_budget_view{.candidate_logical = budget.candidate_logical_bytes,
                                          .candidate_cpu = budget.candidate_cpu_data_bytes,
                                          .application_logical = budget.application_logical_bytes,
                                          .application_cpu = budget.application_cpu_data_bytes,
                                          .available = budget.available_bytes,
                                          .upload = budget.upload_reserved_bytes,
                                          .peak_upload = budget.peak_upload_bytes};
  }
  const auto& reload = state.runtime.asset_reload_status();
  view.reload.publish_result = reload.publish_result;
  view.reload.visible = reload.state != runtime_asset_reload_state::idle;
  if (reload.state == runtime_asset_reload_state::applied) {
    view.reload.tone = asset_status_tone::success;
  } else if (reload.state == runtime_asset_reload_state::failed ||
             reload.state == runtime_asset_reload_state::restart_required) {
    view.reload.tone = asset_status_tone::error;
  }
  view.reload.revision = reload.revision;
  view.reload.message = reload.message;
  view.reload.completed = reload.completed_assets;
  view.reload.total = reload.total_assets;
  view.reload.can_cancel = reload.can_cancel;
  view.reload.can_retry = reload.state == runtime_asset_reload_state::failed;
  view.reload.restart_required = reload.state == runtime_asset_reload_state::restart_required;
  const auto& change = state.author_assets.status();
  view.author_change_visible = change.state != author_asset_change_state::idle;
  if (change.state == author_asset_change_state::applied) {
    view.author_change_tone = asset_status_tone::success;
  } else if (change.state == author_asset_change_state::conflict ||
             change.state == author_asset_change_state::failed) {
    view.author_change_tone = asset_status_tone::error;
  }
  view.author_uri = change.uri;
  view.author_message = change.message;
  const auto* node = state.session.selected_node();
  if (selected != state.assets.entries().end()) {
    view.can_add_mesh = is_mesh_asset(*selected) &&
                        find_material_for_mesh(state.assets.entries(), *selected) != nullptr;
    view.can_add_prefab =
        selected->kind == asset_browser_kind::authored_asset && is_prefab_asset(*selected);
    view.can_apply =
        node != nullptr && ((!node->material_uri.empty() && is_mesh_asset(*selected)) ||
                            (!node->mesh_uri.empty() && is_material_asset(*selected)));
  }
  view.scene_edit_attempted = state.asset_scene_attempted;
  view.scene_edit_result = state.asset_scene_result;
  const auto request = draw_asset_browser_panel(state.assets, state.asset_filter,
                                                state.panel_visibility.asset_browser, view);
  execute_asset_browser_request(state, request);
}

#endif

bool draw_transform_gizmo(editor_state& state, const ImVec2& minimum, const ImVec2& size) noexcept {
  const auto* selected = state.session.selected_node();
  const auto* prefab = state.session.selected_prefab_node();
  const auto editable_prefab = prefab != nullptr && !prefab->is_instance_root;
  if ((selected == nullptr && !editable_prefab) || size.x <= 1.0F || size.y <= 1.0F) {
    if (state.gizmo_drag.is_active()) {
      state.history_error = state.gizmo_drag.finish(state.session, state.history);
      state.gizmo_wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      ImGuizmo::Enable(false);
      ImGuizmo::Enable(true);
    }
    return false;
  }

  if (state.gizmo_drag.is_active() && !state.gizmo_drag.matches_selection(state.session)) {
    state.history_error = state.gizmo_drag.finish(state.session, state.history);
    state.gizmo_wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImGuizmo::Enable(false);
    ImGuizmo::Enable(true);
    return true;
  }
  if (state.gizmo_wait_release) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      return true;
    }
    state.gizmo_wait_release = false;
  }
  const auto node = selected != nullptr ? selected->node : prefab->node;

  gneiss::transform world = GNEISS_TRANSFORM_IDENTITY;
  auto operation =
      gneiss::from_native(gneiss_scene_node_get_world_transform(state.world, node.get(), &world));
  gneiss::editor::gizmo_matrix model{};
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::transform_to_gizmo_matrix(world, model);
  }
  if (operation != gneiss::result::success) {
    state.history_error = operation;
    return false;
  }

  const auto* viewport = ImGui::GetMainViewport();
  if (viewport == nullptr || viewport->Size.x <= 1.0F || viewport->Size.y <= 1.0F) {
    return false;
  }
  auto view = gneiss::editor::build_gizmo_view_matrix(state.camera.current_transform());
  auto projection =
      gneiss::editor::build_gizmo_projection_matrix(viewport->Size.x / viewport->Size.y);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
  ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);
  ImGui::GetWindowDrawList()->AddText(
      ImVec2(minimum.x + 8.0F, minimum.y + size.y - ImGui::GetTextLineHeight() - 8.0F),
      IM_COL32(205, 214, 244, 210), "Grid: 1 unit = 1 m | adaptive spacing");
  const auto native_operation = state.gizmo_mode == gizmo_operation::translate ? ImGuizmo::TRANSLATE
                                : state.gizmo_mode == gizmo_operation::rotate  ? ImGuizmo::ROTATE
                                                                               : ImGuizmo::SCALE;
  const auto manipulated = ImGuizmo::Manipulate(view.data(), projection.data(), native_operation,
                                                ImGuizmo::WORLD, model.data());
  const auto using_now = ImGuizmo::IsUsing();
  if (using_now && !state.gizmo_drag.is_active()) {
    state.history_error = state.gizmo_drag.begin(state.session);
  }
  if (manipulated && state.gizmo_drag.matches_selection(state.session)) {
    state.history_error = state.gizmo_drag.preview(state.session, state.world, model);
  }
  if (!using_now && state.gizmo_drag.is_active()) {
    state.history_error = state.gizmo_drag.finish(state.session, state.history);
  }
  return using_now || ImGuizmo::IsOver(native_operation);
}

gneiss_result update_editor_camera(editor_state& state, const gneiss_frame_time& time) {
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
    if (const auto* selected = state.session.selected_node(); selected != nullptr) {
      selected_node = selected->node;
    } else if (const auto* prefab = state.session.selected_prefab_node(); prefab != nullptr) {
      selected_node = prefab->node;
    }
    if (selected_node.is_valid()) {
      gneiss_transform target = GNEISS_TRANSFORM_IDENTITY;
      const auto result =
          gneiss_scene_node_get_world_transform(state.world, selected_node.get(), &target);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      return gneiss::to_native(state.camera.focus(target));
    }
  }
  return gneiss::to_native(state.camera.update(input));
}

gneiss_result update_editor(gneiss_application application, const gneiss_frame_time* time,
                            void* user_data) {
  try {
    if (time == nullptr || user_data == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    auto& state = *static_cast<editor_state*>(user_data);
    state.runtime.update();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    if (state.task_scheduler.mode() == gneiss::tasks::execution_mode::cooperative) {
      (void)state.task_scheduler.run_ready();
    }
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
    for (const auto& event : author_events) {
      if (event.kind == gneiss::editor::asset_file_event_kind::error) {
        if (state.author_watch_result == gneiss::result::success) {
          state.author_assets.request_rescan();
        }
        state.author_watch_result = event.operation;
        continue;
      }
      const auto candidate_uri = "asset://" + path_utf8(event.relative_path.lexically_normal());
      const auto affects_open_document =
          candidate_uri == state.session.uri() ||
          std::ranges::any_of(state.session.prefab_nodes(), [&candidate_uri](const auto& node) {
            return node.prefab_uri == candidate_uri;
          });
      const auto change = state.author_assets.observe(
          event.relative_path, state.session.is_dirty() && affects_open_document);
      if (change.state != gneiss::editor::author_asset_change_state::changed) {
        continue;
      }
      if (change.operation != gneiss::result::success) {
        state.author_assets.mark_failed(change.uri, change.operation);
        continue;
      }
      const auto is_current_scene = change.uri == state.session.uri();
      const auto is_used_prefab =
          std::ranges::any_of(state.session.prefab_nodes(), [&change](const auto& node) {
            return node.prefab_uri == change.uri;
          });
      auto operation = gneiss::result::success;
      if (is_current_scene || is_used_prefab) {
        const auto current_uri = std::string{state.session.uri()};
        operation = state.session.open(application, state.world, current_uri);
        if (operation == gneiss::result::success) {
          state.history.clear();
        }
      }
      if (operation == gneiss::result::success) {
        operation =
            state.runtime.publish_asset_revision(std::span<const std::string>(&change.uri, 1U));
      }
      if (operation == gneiss::result::success) {
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
    if (state.asset_shutdown_pending && state.asset_reimports.status().stopped &&
        state.author_assets.stopped()) {
      (void)gneiss_application_request_exit(application);
    }
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
    if (state.runtime_attempted) {
      state.runtime_result = state.runtime.last_result();
    }
    if (state.package_process.has_started()) {
      state.package_process.update();
      if (!state.package_process.is_running()) {
        state.package_result = state.package_process.exit_code() == 0
                                   ? gneiss::result::success
                                   : gneiss::result::dependency_failed;
      }
    }
    auto result = state.ui.begin_frame(application, *time);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    if (state.asset_shutdown_pending) {
      ImGui::Begin("Closing Editor");
      ImGui::TextUnformatted("Waiting for the current asset stage to finish...");
      ImGui::End();
      return state.ui.submit(application);
    }
#endif
    ImGuizmo::BeginFrame();
    // 先收尾再处理菜单、保存和撤销；折叠或隐藏面板也不能遗失已应用的拖动。
    if (state.gizmo_drag.is_active() &&
        (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::GetIO().KeyCtrl ||
         ImGui::IsKeyPressed(ImGuiKey_Delete, false))) {
      state.history_error = state.gizmo_drag.finish(state.session, state.history);
      state.gizmo_wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      ImGuizmo::Enable(false);
      ImGuizmo::Enable(true);
    }
    const auto selection_result = state.session.validate_selection();
    if (selection_result != gneiss::result::success &&
        selection_result != gneiss::result::invalid_handle) {
      return gneiss::to_native(selection_result);
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
        ImGui::BeginDisabled(state.session.is_dirty() || state.runtime.is_busy() ||
                             state.package_process.is_running());
        const auto package_requested = ImGui::MenuItem("Export Package...");
        ImGui::EndDisabled();
        ImGui::Separator();
#endif
        const auto exit_requested = ImGui::MenuItem("Exit");
        if (save_requested) {
          state.save_result = save_document(state);
          state.save_attempted = state.save_result != gneiss::result::not_ready;
        }
        if (save_as_requested) {
          state.save_result = save_document_as(state);
          state.save_attempted = state.save_result != gneiss::result::not_ready;
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
        document_action requested = document_action::none;
        if (new_requested) {
          requested = document_action::new_scene;
        } else if (open_requested) {
          requested = document_action::open_scene;
        } else if (exit_requested) {
          requested = document_action::exit_editor;
        }
        if (requested != document_action::none) {
          request_document_action(state, application, requested);
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Edit")) {
        ImGui::BeginDisabled(!state.history.can_undo());
        const auto undo_requested = ImGui::MenuItem("Undo", "Ctrl+Z");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!state.history.can_redo());
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
            control_state == gneiss::editor::runtime_control_state::running ||
            control_state == gneiss::editor::runtime_control_state::paused;
        ImGui::BeginDisabled(!can_toggle_pause);
        const auto pause_requested = ImGui::MenuItem(
            control_state == gneiss::editor::runtime_control_state::paused ? "Resume" : "Pause",
            "F7");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!state.runtime.is_busy());
        const auto stop_requested = ImGui::MenuItem("Stop", "F8");
        ImGui::EndDisabled();
        if (run_requested) {
          request_runtime_launch(state);
        }
        if (pause_requested) {
          state.runtime_result = control_state == gneiss::editor::runtime_control_state::paused
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
          runtime_control == gneiss::editor::runtime_control_state::running ||
          runtime_control == gneiss::editor::runtime_control_state::paused;
      if (gneiss::editor::toolbar_icon_button(
              "##PauseRuntime", gneiss::editor::toolbar_icon::pause,
              runtime_control == gneiss::editor::runtime_control_state::paused ? "恢复 (F7)"
                                                                               : "暂停 (F7)",
              can_toggle_pause, runtime_control == gneiss::editor::runtime_control_state::paused)) {
        state.runtime_result = runtime_control == gneiss::editor::runtime_control_state::paused
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
    gneiss::editor::begin_editor_workspace();
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
      request_document_action(state, application, document_action::new_scene);
    }
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
      request_document_action(state, application, document_action::open_scene);
    }
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      state.save_result = save_document_as(state);
      state.save_attempted = state.save_result != gneiss::result::not_ready;
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
      if (control == gneiss::editor::runtime_control_state::running ||
          control == gneiss::editor::runtime_control_state::paused) {
        state.runtime_result = control == gneiss::editor::runtime_control_state::paused
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
          gneiss::child_process_start_info info;
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
        if (state.runtime_result == gneiss::result::success) {
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
    if (state.pending_document_action != document_action::none &&
        !ImGui::IsPopupOpen("Unsaved Changes")) {
      ImGui::OpenPopup("Unsaved Changes");
    }
    if (state.pending_document_action != document_action::none &&
        ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("The current scene has unsaved changes.");
      if (ImGui::Button("Save")) {
        state.save_result = save_document(state);
        state.save_attempted = state.save_result != gneiss::result::not_ready;
        if (state.save_result == gneiss::result::success) {
          state.history_error =
              perform_document_action(state, application, state.pending_document_action);
          state.pending_document_action = document_action::none;
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard")) {
        state.history_error =
            perform_document_action(state, application, state.pending_document_action);
        state.pending_document_action = document_action::none;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        state.pending_document_action = document_action::none;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      state.history_error = io.KeyShift ? redo_editor_command(state) : undo_editor_command(state);
    }

    const auto log_path = state.runtime.log_file().generic_string();
    const gneiss::editor::console_runtime_view console_runtime{
        .building = state.runtime.is_building(),
        .running = state.runtime.is_running(),
        .started = state.runtime.has_started(),
        .exit_code = state.runtime.exit_code(),
        .attempted = state.runtime_attempted,
        .operation = state.runtime_result,
        .log_path = log_path};
    gneiss::editor::draw_console_panel(
        state.runtime.console(), state.console_panel, state.panel_visibility.console,
        console_runtime, {.context = &state.runtime, .clear = [](void* context) {
                            static_cast<gneiss::editor::runtime_process*>(context)->clear_output();
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
          ImGui::Text("Fixed updates: %llu | Nodes: %llu | Entities: %llu",
                      static_cast<unsigned long long>(statistics.fixed_update_count),
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
    if (!state.session.is_open()) {
      ImGui::TextUnformatted("No scene is open");
    } else {
      auto pending_action = state.pending_hierarchy_action;
      if (pending_action != hierarchy_action::none) {
        if (const auto* pending = state.session.find_node(state.pending_hierarchy_uuid);
            pending != nullptr) {
          (void)state.session.select(pending->node);
        } else {
          pending_action = hierarchy_action::none;
        }
        state.pending_hierarchy_action = hierarchy_action::none;
        state.pending_hierarchy_uuid.clear();
      }
      const auto* selected = state.session.selected_node();
      const auto can_edit_selection = selected != nullptr;
      const auto hierarchy_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
      const auto keyboard_enabled = hierarchy_focused && !ImGui::GetIO().WantTextInput;
      const auto create_requested = ImGui::Button("Create Empty");
      ImGui::SameLine();
      ImGui::BeginDisabled(selected == nullptr);
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
          const auto* current = state.session.find_node(state.rename_uuid);
          state.history_error = current == nullptr ? gneiss::result::not_found
                                                   : state.session.rename_node(current->node, next);
          if (state.history_error == gneiss::result::success) {
            const auto uuid = state.rename_uuid;
            const auto previous = state.rename_previous;
            state.history_error = state.history.record(
                {.label = "重命名节点",
                 .undo =
                     [&state, uuid, previous] {
                       const auto* node = state.session.find_node(uuid);
                       return node == nullptr ? gneiss::result::not_found
                                              : state.session.rename_node(node->node, previous);
                     },
                 .redo =
                     [&state, uuid, next] {
                       const auto* node = state.session.find_node(uuid);
                       return node == nullptr ? gneiss::result::not_found
                                              : state.session.rename_node(node->node, next);
                     },
                 .merge_key = {}});
            if (state.history_error != gneiss::result::success) {
              if (const auto* node = state.session.find_node(uuid); node != nullptr) {
                (void)state.session.rename_node(node->node, previous);
              }
            }
          }
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      if (create_requested) {
        const auto parent = selected == nullptr ? gneiss::scene_node_id{} : selected->node;
        gneiss::scene_node_id created;
        state.history_error = state.session.create_node("Node", parent, created);
        if (state.history_error == gneiss::result::success) {
          const auto uuid = state.session.selected_node()->uuid;
          auto snapshot = std::make_shared<gneiss::editor::scene_subtree_snapshot>();
          state.history_error = state.history.record(
              {.label = "创建节点",
               .undo =
                   [&state, uuid, snapshot] {
                     const auto* current = state.session.find_node(uuid);
                     return current == nullptr
                                ? gneiss::result::not_found
                                : state.session.destroy_subtree(current->node, *snapshot);
                   },
               .redo =
                   [&state, snapshot] {
                     gneiss::scene_node_id restored;
                     return state.session.restore_subtree(*snapshot, restored);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::result::success) {
            if (const auto* current = state.session.find_node(uuid); current != nullptr) {
              (void)state.session.destroy_subtree(current->node, *snapshot);
            }
          }
        }
      }
      if (duplicate_requested) {
        const auto parent = selected->parent;
        gneiss::scene_node_id duplicate;
        state.history_error = state.session.duplicate_subtree(selected->node, parent, duplicate);
        if (state.history_error == gneiss::result::success) {
          const auto uuid = state.session.selected_node()->uuid;
          auto snapshot = std::make_shared<gneiss::editor::scene_subtree_snapshot>();
          state.history_error = state.history.record(
              {.label = "复制子树",
               .undo =
                   [&state, uuid, snapshot] {
                     const auto* current = state.session.find_node(uuid);
                     return current == nullptr
                                ? gneiss::result::not_found
                                : state.session.destroy_subtree(current->node, *snapshot);
                   },
               .redo =
                   [&state, snapshot] {
                     gneiss::scene_node_id restored;
                     return state.session.restore_subtree(*snapshot, restored);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::result::success) {
            if (const auto* current = state.session.find_node(uuid); current != nullptr) {
              (void)state.session.destroy_subtree(current->node, *snapshot);
            }
          }
        }
      }
      if (delete_requested) {
        const auto was_dirty = state.session.is_dirty();
        const auto node = selected->node;
        gneiss::editor::scene_subtree_snapshot snapshot;
        state.history_error = state.session.destroy_subtree(node, snapshot);
        if (state.history_error == gneiss::result::success) {
          state.history_error = state.history.record(
              {.label = "删除节点",
               .undo =
                   [&state, snapshot] {
                     gneiss::scene_node_id restored;
                     return state.session.restore_subtree(snapshot, restored);
                   },
               .redo =
                   [&state, uuid = snapshot.root_uuid] {
                     const auto* current = state.session.find_node(uuid);
                     if (current == nullptr) {
                       return gneiss::result::not_found;
                     }
                     gneiss::editor::scene_subtree_snapshot discarded;
                     return state.session.destroy_subtree(current->node, discarded);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::result::success) {
            gneiss::scene_node_id restored;
            (void)state.session.restore_subtree(snapshot, restored);
            if (!was_dirty) {
              state.session.clear_dirty();
            }
          }
        }
      }
      const auto* prefab_selected = state.session.selected_prefab_node();
      if (prefab_selected != nullptr && prefab_selected->is_instance_root) {
        const auto source_uuid = prefab_selected->instance_uuid;
        if (ImGui::Button("Duplicate Prefab")) {
          auto parent = gneiss::scene_node_id{};
          if (prefab_selected->parent.is_valid()) {
            const auto found = std::ranges::find(state.session.nodes(), prefab_selected->parent,
                                                 &gneiss::editor::scene_node_record::node);
            if (found != state.session.nodes().end()) {
              parent = found->node;
            }
          }
          gneiss::scene_node_id duplicate;
          state.history_error = state.session.create_prefab_instance(
              prefab_selected->display_name, prefab_selected->prefab_uri, parent, duplicate);
          if (state.history_error == gneiss::result::success) {
            const auto duplicate_uuid = state.session.selected_prefab_node()->instance_uuid;
            auto snapshot = std::make_shared<gneiss::editor::prefab_instance_snapshot>();
            state.history_error = state.history.record(
                {.label = "复制 Prefab 实例",
                 .undo =
                     [&state, duplicate_uuid, snapshot] {
                       const auto* current = state.session.find_prefab_root(duplicate_uuid);
                       return current == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.destroy_prefab_instance(current->node, *snapshot);
                     },
                 .redo =
                     [&state, snapshot] {
                       gneiss::scene_node_id restored;
                       return state.session.restore_prefab_instance(*snapshot, restored);
                     },
                 .merge_key = {}});
            if (state.history_error != gneiss::result::success) {
              const auto* current = state.session.find_prefab_root(duplicate_uuid);
              if (current != nullptr) {
                gneiss::editor::prefab_instance_snapshot discarded;
                (void)state.session.destroy_prefab_instance(current->node, discarded);
              }
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Prefab")) {
          gneiss::editor::prefab_instance_snapshot snapshot;
          state.history_error =
              state.session.destroy_prefab_instance(prefab_selected->node, snapshot);
          if (state.history_error == gneiss::result::success) {
            state.history_error = state.history.record(
                {.label = "删除 Prefab 实例",
                 .undo =
                     [&state, snapshot] {
                       gneiss::scene_node_id restored;
                       return state.session.restore_prefab_instance(snapshot, restored);
                     },
                 .redo =
                     [&state, uuid = snapshot.instance_uuid] {
                       const auto* current = state.session.find_prefab_root(uuid);
                       if (current == nullptr) {
                         return gneiss::result::not_found;
                       }
                       gneiss::editor::prefab_instance_snapshot discarded;
                       return state.session.destroy_prefab_instance(current->node, discarded);
                     },
                 .merge_key = {}});
            if (state.history_error != gneiss::result::success) {
              gneiss::scene_node_id restored;
              (void)state.session.restore_prefab_instance(snapshot, restored);
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Prefab")) {
          const auto prefab_uri = prefab_selected->prefab_uri;
          std::vector<std::string> instance_uuids;
          for (const auto& node : state.session.prefab_nodes()) {
            if (node.is_instance_root && node.prefab_uri == prefab_uri) {
              instance_uuids.push_back(node.instance_uuid);
            }
          }
          auto guard = std::make_shared<prefab_refresh_guard>();
          guard->session = &state.session;
          state.history_error = gneiss::result::success;
          for (const auto& instance_uuid : instance_uuids) {
            const auto* current = state.session.find_prefab_root(instance_uuid);
            gneiss::scene_node_id refreshed;
            gneiss_scene_prefab_refresh_token token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
            state.history_error =
                current == nullptr
                    ? gneiss::result::not_found
                    : state.session.refresh_prefab_instance(current->node, refreshed, token);
            if (state.history_error != gneiss::result::success) {
              (void)toggle_prefab_refreshes(state.session, guard->tokens);
              break;
            }
            guard->tokens.push_back(token);
          }
          if (state.history_error == gneiss::result::success) {
            state.history_error = state.history.record(
                {.label = "刷新同源 Prefab 实例",
                 .undo = [&state,
                          guard] { return toggle_prefab_refreshes(state.session, guard->tokens); },
                 .redo = [&state,
                          guard] { return toggle_prefab_refreshes(state.session, guard->tokens); },
                 .merge_key = {}});
            if (state.history_error != gneiss::result::success) {
              (void)toggle_prefab_refreshes(state.session, guard->tokens);
            }
          }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(state.session.is_dirty() || state.runtime.is_busy());
        if (ImGui::Button("Unpack Prefab...")) {
          state.pending_prefab_author_action = prefab_author_action::unpack;
          state.pending_prefab_instance_uuid = prefab_selected->instance_uuid;
        }
        ImGui::EndDisabled();
      }
      const auto tree_request = gneiss::editor::draw_author_hierarchy({
          .nodes = state.session.nodes(),
          .prefab_nodes = state.session.prefab_nodes(),
          .selection = state.session.selection(),
          .can_create_prefab = !state.session.is_dirty() && !state.runtime.is_busy(),
      });
      using tree_action = gneiss::editor::author_hierarchy_action;
      switch (tree_request.action) {
      case tree_action::none:
        break;
      case tree_action::select:
        (void)state.session.select(tree_request.node);
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
        const auto path = std::string{"prefabs/"} + tree_request.uuid + ".prefab.json";
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
          ImGui::TextUnformatted("Create the selected subtree as a Prefab source.");
          ImGui::InputText("Asset path", state.prefab_path_buffer.data(),
                           state.prefab_path_buffer.size());
          break;
        case prefab_author_action::apply:
          ImGui::TextUnformatted("Apply this instance's Transform overrides to the shared source?");
          ImGui::TextDisabled("All instances using the source will be reloaded.");
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
                create_prefab_from_selected(state, application, state.pending_prefab_root_uuid,
                                            std::string_view{state.prefab_path_buffer.data()});
            break;
          case prefab_author_action::apply:
            state.prefab_author_result =
                apply_selected_prefab(state, application, state.pending_prefab_instance_uuid,
                                      state.pending_prefab_source_uuid);
            break;
          case prefab_author_action::unpack:
            state.prefab_author_result =
                unpack_selected_prefab(state, application, state.pending_prefab_instance_uuid);
            break;
          case prefab_author_action::none:
            state.prefab_author_result = gneiss::result::invalid_state;
            break;
          }
          state.prefab_author_busy = false;
          state.prefab_author_attempted = true;
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

    if (state.history_error != gneiss::result::success &&
        state.history_error != gneiss::result::not_ready) {
      const auto message = state.history_error.message();
      ImGui::SetNextWindowPos(ImVec2(500.0F, 24.0F));
      ImGui::Begin("Command Error", nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration);
      ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                         static_cast<int>(message.size()), message.data());
      ImGui::End();
    }
    if (state.prefab_author_attempted && state.prefab_author_result != gneiss::result::success) {
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
      state.history_error = state.gizmo_drag.finish(state.session, state.history);
      state.gizmo_wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      ImGuizmo::Enable(false);
      ImGuizmo::Enable(true);
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
      if (const auto* selected = state.session.selected_node(); selected != nullptr) {
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
        if (camera_result != GNEISS_SUCCESS) {
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
    ImGui::BeginDisabled(!state.session.is_open());
    const auto save_button_pressed = ImGui::Button("Save");
    ImGui::EndDisabled();
    const auto save_requested =
        state.session.is_open() &&
        (save_button_pressed || (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
                                 ImGui::IsKeyPressed(ImGuiKey_S, false)));
    ImGui::SameLine();
    if (!state.session.is_open()) {
      ImGui::TextDisabled("No scene");
    } else if (state.session.is_dirty()) {
      ImGui::TextColored(gneiss::editor::theme_warning_color(), "Modified");
    } else {
      ImGui::TextColored(gneiss::editor::theme_success_color(), "Saved");
    }
    if (save_requested) {
      state.save_result = save_document(state);
      state.save_attempted = state.save_result != gneiss::result::not_ready;
    }
    if (state.save_attempted && state.save_result != gneiss::result::success) {
      const auto message = state.save_result.message();
      ImGui::TextColored(gneiss::editor::theme_error_color(), "Save failed: %.*s",
                         static_cast<int>(message.size()), message.data());
    }
    ImGui::Separator();
    if (const auto* runtime_selected = gneiss::editor::selected_runtime_node(
            state.runtime.scene_mirror(), state.runtime_selection);
        runtime_selected != nullptr) {
      state.inspector.clear();
      state.inspected_entity = {};
      state.inspector_error = gneiss::result::success;
      gneiss::editor::draw_runtime_inspector(*runtime_selected,
                                             runtime_inspector_actions(state, *runtime_selected));
    } else if (const auto* prefab = state.session.selected_prefab_node(); prefab != nullptr) {
      state.inspector.clear();
      state.inspected_entity = {};
      state.inspector_error = gneiss::result::success;
      ImGui::TextUnformatted(prefab->is_instance_root ? "Prefab Instance" : "Prefab Source Node");
      ImGui::Text("Name: %s", prefab->display_name.c_str());
      ImGui::Text("Instance UUID: %s", prefab->instance_uuid.c_str());
      if (!prefab->source_node_uuid.empty()) {
        ImGui::Text("Source UUID: %s", prefab->source_node_uuid.c_str());
      }
      ImGui::Text("Prefab: %s", prefab->prefab_uri.c_str());
      ImGui::Text("Entity: %llu", static_cast<unsigned long long>(prefab->entity.get()));
      if (!prefab->is_instance_root) {
        ImGui::BeginDisabled(prefab->override_flags == 0U || state.session.is_dirty() ||
                             state.runtime.is_busy());
        if (ImGui::Button("Apply Overrides to Prefab...")) {
          state.pending_prefab_author_action = prefab_author_action::apply;
          state.pending_prefab_instance_uuid = prefab->instance_uuid;
          state.pending_prefab_source_uuid = prefab->source_node_uuid;
        }
        ImGui::EndDisabled();
      }
      ImGui::Separator();
      if (prefab->is_read_only) {
        const auto instance_uuid = prefab->instance_uuid;
        const auto source_uuid = prefab->source_node_uuid;
        const auto status = [&](const char* label, std::uint32_t flag) {
          ImGui::Text("%s: %s", label,
                      (prefab->override_flags & flag) != 0U ? "Overridden" : "Inherited");
        };
        status("Translation", GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN);
        status("Rotation", GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN);
        status("Scale", GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN);
        ImGui::TextDisabled("Source T: %.3f, %.3f, %.3f",
                            prefab->source_local_transform.translation[0],
                            prefab->source_local_transform.translation[1],
                            prefab->source_local_transform.translation[2]);
        ImGui::TextDisabled(
            "Source R: %.3f, %.3f, %.3f, %.3f", prefab->source_local_transform.rotation[0],
            prefab->source_local_transform.rotation[1], prefab->source_local_transform.rotation[2],
            prefab->source_local_transform.rotation[3]);
        ImGui::TextDisabled("Source S: %.3f, %.3f, %.3f", prefab->source_local_transform.scale[0],
                            prefab->source_local_transform.scale[1],
                            prefab->source_local_transform.scale[2]);
        ImGui::Separator();
        auto edited = prefab->local_transform;
        const auto previous = prefab->local_transform;
        std::array<float, 3> rotation{};
        const gneiss_property_quaternion quaternion{edited.rotation[0], edited.rotation[1],
                                                    edited.rotation[2], edited.rotation[3]};
        (void)gneiss::editor::quaternion_to_euler_degrees(quaternion, rotation);
        bool changed = ImGui::DragFloat3("Translation", edited.translation, 0.05F);
        if (ImGui::DragFloat3("Rotation (degrees)", rotation.data(), 0.25F, 0.0F, 0.0F, "%.1f°")) {
          gneiss_property_quaternion converted{};
          if (gneiss::editor::euler_degrees_to_quaternion(rotation, converted) ==
              gneiss::result::success) {
            edited.rotation[0] = converted.x;
            edited.rotation[1] = converted.y;
            edited.rotation[2] = converted.z;
            edited.rotation[3] = converted.w;
            changed = true;
          }
        }
        changed = ImGui::DragFloat3("Scale", edited.scale, 0.05F) || changed;
        if (changed) {
          const auto* current = state.session.find_prefab_source(instance_uuid, source_uuid);
          state.history_error = current == nullptr
                                    ? gneiss::result::not_found
                                    : state.session.set_local_transform(current->node, edited);
          if (state.history_error == gneiss::result::success) {
            state.history_error = state.history.record(
                {.label = "变换 Prefab 来源节点",
                 .undo =
                     [&state, instance_uuid, source_uuid, previous] {
                       const auto* node =
                           state.session.find_prefab_source(instance_uuid, source_uuid);
                       return node == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.set_local_transform(node->node, previous);
                     },
                 .redo =
                     [&state, instance_uuid, source_uuid, edited] {
                       const auto* node =
                           state.session.find_prefab_source(instance_uuid, source_uuid);
                       return node == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.set_local_transform(node->node, edited);
                     },
                 .merge_key = "prefab-source-transform:" + instance_uuid + ":" + source_uuid});
          }
        }
        const auto restore_field = [&](const char* label, std::uint32_t flag,
                                       gneiss_field_id field_id) {
          ImGui::BeginDisabled((prefab->override_flags & flag) == 0U);
          const bool requested = ImGui::Button(label);
          ImGui::EndDisabled();
          if (!requested) {
            return;
          }
          const auto* current = state.session.find_prefab_source(instance_uuid, source_uuid);
          gneiss::transform restored_previous{};
          state.history_error = current == nullptr
                                    ? gneiss::result::not_found
                                    : state.session.restore_prefab_transform_field(
                                          current->node, field_id, restored_previous);
          current = state.session.find_prefab_source(instance_uuid, source_uuid);
          if (state.history_error != gneiss::result::success || current == nullptr) {
            return;
          }
          const auto restored = current->local_transform;
          state.history_error = state.history.record(
              {.label = "恢复 Prefab 来源字段",
               .undo =
                   [&state, instance_uuid, source_uuid, restored_previous] {
                     const auto* node =
                         state.session.find_prefab_source(instance_uuid, source_uuid);
                     return node == nullptr
                                ? gneiss::result::not_found
                                : state.session.set_local_transform(node->node, restored_previous);
                   },
               .redo =
                   [&state, instance_uuid, source_uuid, restored] {
                     const auto* node =
                         state.session.find_prefab_source(instance_uuid, source_uuid);
                     return node == nullptr
                                ? gneiss::result::not_found
                                : state.session.set_local_transform(node->node, restored);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::result::success) {
            if (const auto* node = state.session.find_prefab_source(instance_uuid, source_uuid);
                node != nullptr) {
              (void)state.session.set_local_transform(node->node, restored_previous);
            }
          }
        };
        restore_field("Restore Translation", GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN,
                      GNEISS_TRANSFORM_FIELD_TRANSLATION);
        ImGui::SameLine();
        restore_field("Restore Rotation", GNEISS_SCENE_PREFAB_NODE_ROTATION_OVERRIDDEN,
                      GNEISS_TRANSFORM_FIELD_ROTATION);
        ImGui::SameLine();
        restore_field("Restore Scale", GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN,
                      GNEISS_TRANSFORM_FIELD_SCALE);
        const auto* current = state.session.find_prefab_source(instance_uuid, source_uuid);
        const bool has_override = current != nullptr && current->override_flags != 0U;
        ImGui::BeginDisabled(!has_override);
        const bool restore_all = ImGui::Button("Restore All Transform");
        ImGui::EndDisabled();
        if (restore_all) {
          gneiss::transform all_previous{};
          state.history_error = state.session.restore_prefab_transform(current->node, all_previous);
          current = state.session.find_prefab_source(instance_uuid, source_uuid);
          if (state.history_error == gneiss::result::success && current != nullptr) {
            const auto restored = current->local_transform;
            state.history_error = state.history.record(
                {.label = "恢复 Prefab 来源变换",
                 .undo =
                     [&state, instance_uuid, source_uuid, all_previous] {
                       const auto* node =
                           state.session.find_prefab_source(instance_uuid, source_uuid);
                       return node == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.set_local_transform(node->node, all_previous);
                     },
                 .redo =
                     [&state, instance_uuid, source_uuid, restored] {
                       const auto* node =
                           state.session.find_prefab_source(instance_uuid, source_uuid);
                       return node == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.set_local_transform(node->node, restored);
                     },
                 .merge_key = {}});
            if (state.history_error != gneiss::result::success) {
              if (const auto* node = state.session.find_prefab_source(instance_uuid, source_uuid);
                  node != nullptr) {
                (void)state.session.set_local_transform(node->node, all_previous);
              }
            }
          }
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
            const auto* current = state.session.find_prefab_root(state.rename_uuid);
            state.history_error = current == nullptr
                                      ? gneiss::result::not_found
                                      : state.session.rename_prefab_instance(current->node, next);
            if (state.history_error == gneiss::result::success) {
              const auto uuid = state.rename_uuid;
              const auto previous = state.rename_previous;
              state.history_error = state.history.record(
                  {.label = "重命名 Prefab 实例",
                   .undo =
                       [&state, uuid, previous] {
                         const auto* node = state.session.find_prefab_root(uuid);
                         return node == nullptr
                                    ? gneiss::result::not_found
                                    : state.session.rename_prefab_instance(node->node, previous);
                       },
                   .redo =
                       [&state, uuid, next] {
                         const auto* node = state.session.find_prefab_root(uuid);
                         return node == nullptr
                                    ? gneiss::result::not_found
                                    : state.session.rename_prefab_instance(node->node, next);
                       },
                   .merge_key = {}});
            }
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }
        auto edited = prefab->local_transform;
        std::array<float, 3> rotation{};
        const gneiss_property_quaternion quaternion{edited.rotation[0], edited.rotation[1],
                                                    edited.rotation[2], edited.rotation[3]};
        (void)gneiss::editor::quaternion_to_euler_degrees(quaternion, rotation);
        const auto previous = prefab->local_transform;
        bool changed = ImGui::DragFloat3("Translation", edited.translation, 0.05F);
        if (ImGui::DragFloat3("Rotation (degrees)", rotation.data(), 0.25F, 0.0F, 0.0F, "%.1f°")) {
          gneiss_property_quaternion converted{};
          if (gneiss::editor::euler_degrees_to_quaternion(rotation, converted) ==
              gneiss::result::success) {
            edited.rotation[0] = converted.x;
            edited.rotation[1] = converted.y;
            edited.rotation[2] = converted.z;
            edited.rotation[3] = converted.w;
            changed = true;
          }
        }
        changed = ImGui::DragFloat3("Scale", edited.scale, 0.05F) || changed;
        if (changed) {
          const auto* current = state.session.find_prefab_root(instance_uuid);
          state.history_error = current == nullptr
                                    ? gneiss::result::not_found
                                    : state.session.set_local_transform(current->node, edited);
          if (state.history_error == gneiss::result::success) {
            state.history_error = state.history.record(
                {.label = "变换 Prefab 实例",
                 .undo =
                     [&state, instance_uuid, previous] {
                       const auto* node = state.session.find_prefab_root(instance_uuid);
                       return node == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.set_local_transform(node->node, previous);
                     },
                 .redo =
                     [&state, instance_uuid, edited] {
                       const auto* node = state.session.find_prefab_root(instance_uuid);
                       return node == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.set_local_transform(node->node, edited);
                     },
                 .merge_key = "prefab-transform:" + instance_uuid});
          }
        }
      }
    } else if (const auto* selected = state.session.selected_node(); selected != nullptr) {
      if (state.inspected_entity != selected->entity) {
        state.inspector_error = state.inspector.refresh(state.world, selected->entity);
        state.inspected_entity = selected->entity;
      }
      ImGui::Text("Name: %s", selected->display_name.c_str());
      ImGui::Text("UUID: %s", selected->uuid.c_str());
      ImGui::Text("Entity: %llu", static_cast<unsigned long long>(selected->entity.get()));
      ImGui::Separator();
      const auto uuid = selected->uuid;
      const auto has_camera =
          (selected->component_flags & GNEISS_SCENE_NODE_COMPONENT_CAMERA) != 0U;
      const auto has_mesh =
          (selected->component_flags & GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER) != 0U;
      const auto selected_mesh_uri = selected->mesh_uri;
      const auto selected_material_uri = selected->material_uri;
      bool components_changed = false;
      if (ImGui::Button(has_camera ? "Remove Camera" : "Add Camera")) {
        if (has_camera) {
          gneiss::scene_camera_desc previous = GNEISS_SCENE_CAMERA_DESC_INIT;
          previous.camera = selected->camera;
          previous.is_primary = selected->is_primary_camera ? 1U : 0U;
          state.history_error = state.session.remove_camera(selected->node);
          if (state.history_error == gneiss::result::success) {
            state.history_error = state.history.record(
                {.label = "移除 Camera",
                 .undo =
                     [&state, uuid, previous] {
                       const auto* node = state.session.find_node(uuid);
                       return node == nullptr ? gneiss::result::not_found
                                              : state.session.set_camera(node->node, previous);
                     },
                 .redo =
                     [&state, uuid] {
                       const auto* node = state.session.find_node(uuid);
                       return node == nullptr ? gneiss::result::not_found
                                              : state.session.remove_camera(node->node);
                     },
                 .merge_key = {}});
            if (state.history_error != gneiss::result::success) {
              if (const auto* node = state.session.find_node(uuid); node != nullptr) {
                (void)state.session.set_camera(node->node, previous);
              }
            }
          }
        } else {
          gneiss::scene_camera_desc camera = GNEISS_SCENE_CAMERA_DESC_INIT;
          state.history_error = state.session.set_camera(selected->node, camera);
          if (state.history_error == gneiss::result::success) {
            state.history_error = state.history.record(
                {.label = "添加 Camera",
                 .undo =
                     [&state, uuid] {
                       const auto* node = state.session.find_node(uuid);
                       return node == nullptr ? gneiss::result::not_found
                                              : state.session.remove_camera(node->node);
                     },
                 .redo =
                     [&state, uuid, camera] {
                       const auto* node = state.session.find_node(uuid);
                       return node == nullptr ? gneiss::result::not_found
                                              : state.session.set_camera(node->node, camera);
                     },
                 .merge_key = {}});
            if (state.history_error != gneiss::result::success) {
              if (const auto* node = state.session.find_node(uuid); node != nullptr) {
                (void)state.session.remove_camera(node->node);
              }
            }
          }
        }
        state.inspected_entity = {};
        components_changed = state.history_error == gneiss::result::success;
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!has_mesh);
      const auto remove_mesh_requested = ImGui::Button("Remove Mesh Renderer");
      ImGui::EndDisabled();
      if (remove_mesh_requested) {
        state.history_error = state.session.remove_mesh_renderer(selected->node);
        if (state.history_error == gneiss::result::success) {
          state.history_error = state.history.record(
              {.label = "移除 Mesh Renderer",
               .undo =
                   [&state, uuid, selected_mesh_uri, selected_material_uri] {
                     const auto* node = state.session.find_node(uuid);
                     return node == nullptr
                                ? gneiss::result::not_found
                                : state.session.set_mesh_renderer(node->node, selected_mesh_uri,
                                                                  selected_material_uri);
                   },
               .redo =
                   [&state, uuid] {
                     const auto* node = state.session.find_node(uuid);
                     return node == nullptr ? gneiss::result::not_found
                                            : state.session.remove_mesh_renderer(node->node);
                   },
               .merge_key = {}});
          if (state.history_error != gneiss::result::success) {
            if (const auto* node = state.session.find_node(uuid); node != nullptr) {
              (void)state.session.set_mesh_renderer(node->node, selected_mesh_uri,
                                                    selected_material_uri);
            }
          }
        }
        state.inspected_entity = {};
        components_changed = state.history_error == gneiss::result::success;
      }
      if (!has_mesh) {
        ImGui::TextDisabled("Select a mesh in Asset Browser to add Mesh Renderer");
      }
      ImGui::Separator();
      if (!components_changed) {
        draw_reflected_properties(state);
      }
    } else {
      state.inspector.clear();
      state.inspected_entity = {};
      state.inspector_error = gneiss::result::success;
      ImGui::TextUnformatted("No node is selected");
    }
    ImGui::End();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    draw_asset_browser(state);
#endif
    if (state.show_imgui_demo) {
      ImGui::ShowDemoWindow(&state.show_imgui_demo);
    }
    // 相机可能被本帧滚轮、聚焦或场景操作更新；网格必须使用最终相机状态。
    const auto grid_result = submit_editor_grid(application, state);
    if (grid_result != gneiss::result::success) {
      return gneiss::to_native(grid_result);
    }
    return state.ui.submit(application);
  } catch (const std::bad_alloc&) {
    // C++ 异常不得越过 C ABI 回调边界。
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

uint8_t handle_close_requested(gneiss_application application, void* user_data) noexcept {
  (void)application;
  if (user_data == nullptr) {
    return 1U;
  }
  auto& state = *static_cast<editor_state*>(user_data);
  if (!state.session.is_dirty()) {
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    state.asset_shutdown_pending = true;
    state.asset_reimports.request_stop();
    state.author_assets.request_stop();
    return state.asset_reimports.status().stopped && state.author_assets.stopped() ? 1U : 0U;
#else
    return 1U;
#endif
  }
  state.pending_document_action = document_action::exit_editor;
  return 0U;
}

int run_editor(int argc, char** argv) {
  launch_options options;
  if (!parse_options(argc, argv, options)) {
    std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=命令行解析，结果=%d，消息=参数无效\n",
                 GNEISS_ERROR_INVALID_ARGUMENT);
    return 64;
  }
  gneiss::editor::editor_project project;
  if (options.project.empty()) {
    const auto operation = gneiss::editor::run_project_manager(options.smoke, project);
    if (operation == gneiss::result::not_ready) {
      return 0;
    }
    if (operation != gneiss::result::success) {
      const auto message = operation.message();
      std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=Project Manager，结果=%d，消息=%.*s\n",
                   gneiss::to_native(operation), static_cast<int>(message.size()), message.data());
      return 65;
    }
  } else {
    const auto operation = gneiss::editor::load_editor_project(utf8_path(options.project), project);
    if (operation != gneiss::result::success) {
      const auto message = operation.message();
      std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=工程加载，结果=%d，消息=%.*s，路径=%s\n",
                   gneiss::to_native(operation), static_cast<int>(message.size()), message.data(),
                   options.project.c_str());
      return 65;
    }
  }
  const auto recovery = gneiss::editor::recover_native_author_transactions(project.asset_root);
  if (recovery != gneiss::result::success) {
    report_startup_failure("作者事务恢复", recovery, path_utf8(project.asset_root));
    return 66;
  }
  const auto asset_root_text = path_utf8(project.asset_root);
  if (asset_root_text.size() > std::numeric_limits<std::uint32_t>::max()) {
    report_startup_failure("资产根校验", gneiss::result::invalid_argument, asset_root_text);
    return 64;
  }
  editor_state state(options.cooperative_tasks);
  gneiss::application application;
  state.asset_root = project.asset_root;
  state.project_root = project.project_root;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  state.asset_reimports.start(state.project_root, state.asset_root);
  state.asset_reimports.request_refresh();
  start_source_asset_watch(state);
  const auto author_monitor_result = state.author_assets.initialize(state.asset_root);
  if (author_monitor_result != gneiss::result::success) {
    const auto message = author_monitor_result.message();
    std::fprintf(stderr, "Gneiss Editor 作者资产监视初始化失败：结果=%d，消息=%.*s\n",
                 gneiss::to_native(author_monitor_result), static_cast<int>(message.size()),
                 message.data());
  } else {
    const auto author_watcher_result = state.author_asset_watcher.start(state.asset_root);
    state.author_watch_result = author_watcher_result;
    if (author_watcher_result != gneiss::result::success) {
      const auto message = author_watcher_result.message();
      std::fprintf(stderr, "Gneiss Editor 作者资产监听启动失败：结果=%d，消息=%.*s\n",
                   gneiss::to_native(author_watcher_result), static_cast<int>(message.size()),
                   message.data());
    }
  }
#endif
  const auto title = project.name + " - Gneiss Editor";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_editor;
  desc.close_requested = handle_close_requested;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.window_width = 1280;
  desc.window_height = 720;
  desc.window_flags = GNEISS_APPLICATION_WINDOW_VISIBLE_BIT |
                      GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT |
                      GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT;
  desc.asset_root = asset_root_text.c_str();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root_text.size());
  desc.environment_asset =
      project.environment.asset.empty() ? nullptr : project.environment.asset.data();
  desc.environment_asset_length = static_cast<std::uint32_t>(project.environment.asset.size());
  desc.environment_intensity = project.environment.intensity;
  desc.environment_rotation_radians =
      project.environment.rotation_degrees * 0.01745329251994329577F;

  auto operation = gneiss::application::create(desc, application);
  if (operation != gneiss::result::success) {
    report_startup_failure("Editor Application 创建", operation, path_utf8(project.project_root));
    return 1;
  }
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  operation = gneiss::from_native(
      gneiss::application_internal::attach_task_executor(application.get(), state.task_scheduler));
  if (operation != gneiss::result::success) {
    return 2;
  }
#endif
  operation = gneiss::from_native(state.ui.initialize(application.get()));
  if (operation != gneiss::result::success) {
    report_startup_failure("Editor UI 初始化", operation);
    return 2;
  }
  if (!options.smoke) {
    const auto layout_result = gneiss::editor::initialize_editor_layout(
        gneiss::editor::default_editor_state_path(), project.project_root, state.panel_visibility);
    if (layout_result != gneiss::result::success &&
        layout_result != gneiss::result::invalid_argument) {
      const auto message = layout_result.message();
      std::fprintf(stderr, "Gneiss Editor 布局加载失败：结果=%d，消息=%.*s\n",
                   gneiss::to_native(layout_result), static_cast<int>(message.size()),
                   message.data());
    }
  }
  operation = state.inspector.initialize();
  if (operation == gneiss::result::success) {
    operation = application.get_world(state.world);
  }
  if (operation != gneiss::result::success) {
    report_startup_failure("Editor World/Inspector 初始化", operation);
  } else {
    operation = state.session.open(application.get(), state.world, project.startup_scene);
    if (operation != gneiss::result::success) {
      report_startup_failure("启动场景打开", operation, project.startup_scene);
    }
  }
  if (operation == gneiss::result::success) {
    operation = state.camera.initialize(state.world);
    if (operation != gneiss::result::success) {
      report_startup_failure("Editor Camera 初始化", operation);
    }
  }
  if (operation != gneiss::result::success) {
    state.ui.shutdown(application.get());
    state.session.close();
    return 3;
  }
  state.history.clear();
  const auto run_result = application.run(options.smoke ? 3U : 0U);
  if (!options.smoke) {
    const auto save_layout_result = gneiss::editor::save_editor_layout(state.panel_visibility);
    if (save_layout_result != gneiss::result::success) {
      const auto message = save_layout_result.message();
      std::fprintf(stderr, "Gneiss Editor 布局保存失败：结果=%d，消息=%.*s\n",
                   gneiss::to_native(save_layout_result), static_cast<int>(message.size()),
                   message.data());
    }
  }
  state.ui.shutdown(application.get());
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  if (state.asset_watcher.is_running()) {
    (void)state.asset_watcher.stop();
  }
  if (state.author_asset_watcher.is_running()) {
    (void)state.author_asset_watcher.stop();
  }
#endif
  state.camera.shutdown();
  state.session.close();
  if (run_result != gneiss::result::success) {
    report_startup_failure("Editor 运行时事件循环", run_result, path_utf8(project.project_root));
  }
  return run_result == gneiss::result::success ? 0 : 4;
}

} // namespace

int main(int argc, char** argv) {
  try {
    return run_editor(argc, argv);
  } catch (...) {
    return 99;
  }
}
