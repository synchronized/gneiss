// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_camera.h"
#include "editor_command_history.h"
#include "editor_project.h"
#include "editor_rotation_math.h"
#include "editor_session.h"
#include "editor_theme.h"
#include "editor_ui.h"
#include "imgui_adapter.h"
#include "native_author_transaction.h"
#include "native_dialog.h"
#include "prefab_authoring.h"
#include "project_manager.h"
#include "project_workspace.h"
#include "property_inspector_model.h"
#include "runtime_author_apply.h"
#include "runtime_launch.h"
#include "runtime_process.h"
#include "transform_gizmo_math.h"
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
#include "asset_browser_model.h"
#include "asset_file_watcher.h"
#include "asset_import_controller.h"
#include "asset_reimport_queue.h"
#include "author_asset_monitor.h"
#endif

#include <gneiss/application.hpp>

#include <ImGuizmo.h>
#include <imgui.h>

#include <algorithm>
#include <array>
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
  gneiss::editor::imgui_adapter ui;
  gneiss::editor::editor_camera camera;
  gneiss::editor::editor_session session;
  gneiss::editor::editor_command_history history;
  gneiss::editor::property_inspector_model inspector;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::entity_id inspected_entity;
  gneiss::ipc_runtime_object_id inspected_runtime_node;
  std::uint64_t inspected_runtime_session = 0U;
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
  gneiss::editor::console_filter console_filter;
  std::array<char, 128> console_search{};
  std::array<char, 96> console_source{};
  std::array<char, 96> console_category{};
  bool console_paused = false;
  bool console_auto_scroll = true;
  std::uint64_t console_pause_entry_id = 0U;
  bool pending_save_and_run = false;
  bool show_imgui_demo = false;
  gneiss::editor::editor_panel_visibility panel_visibility;
  gizmo_operation gizmo_mode = gizmo_operation::translate;
  bool gizmo_using = false;
  bool gizmo_was_dirty = false;
  std::string gizmo_uuid;
  std::string gizmo_instance_uuid;
  std::string gizmo_source_uuid;
  gneiss::transform gizmo_initial_local = GNEISS_TRANSFORM_IDENTITY;
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
  gneiss::editor::asset_file_watcher author_asset_watcher;
  gneiss::editor::author_asset_monitor author_assets;
  gneiss::editor::asset_reimport_queue asset_reimports;
  gneiss::editor::asset_browser_result asset_result = gneiss::editor::asset_browser_result::success;
  ImGuiTextFilter asset_filter;
  gneiss::editor::editor_import_report last_import;
  bool import_attempted = false;
  gneiss::result asset_scene_result = gneiss::result::success;
  bool asset_scene_attempted = false;
#endif
};

constexpr std::size_t matrix_index(std::size_t row, std::size_t column) noexcept {
  return (column * 4U) + row;
}

gneiss::editor::gizmo_matrix build_view_matrix(const gneiss::transform& camera) noexcept {
  auto inverse = camera;
  inverse.rotation[0] = -inverse.rotation[0];
  inverse.rotation[1] = -inverse.rotation[1];
  inverse.rotation[2] = -inverse.rotation[2];
  inverse.translation[0] = 0.0F;
  inverse.translation[1] = 0.0F;
  inverse.translation[2] = 0.0F;
  inverse.scale[0] = 1.0F;
  inverse.scale[1] = 1.0F;
  inverse.scale[2] = 1.0F;
  gneiss::editor::gizmo_matrix result{};
  (void)gneiss::editor::transform_to_gizmo_matrix(inverse, result);
  for (std::size_t row = 0; row < 3U; ++row) {
    result[matrix_index(row, 3U)] = -((result[matrix_index(row, 0U)] * camera.translation[0]) +
                                      (result[matrix_index(row, 1U)] * camera.translation[1]) +
                                      (result[matrix_index(row, 2U)] * camera.translation[2]));
  }
  return result;
}

gneiss::editor::gizmo_matrix build_gizmo_projection_matrix(float aspect) noexcept {
  constexpr float field_of_view = 1.04719755F;
  constexpr float near_plane = 0.1F;
  constexpr float far_plane = 1000.0F;
  const auto focal = 1.0F / std::tan(field_of_view * 0.5F);
  gneiss::editor::gizmo_matrix result{};
  result[matrix_index(0U, 0U)] = focal / aspect;
  // ImGuizmo 会把 NDC Y 转换为向下增长的屏幕坐标，不能重复使用 Vulkan 的 Y 翻转。
  result[matrix_index(1U, 1U)] = focal;
  result[matrix_index(2U, 2U)] = far_plane / (near_plane - far_plane);
  result[matrix_index(2U, 3U)] = (far_plane * near_plane) / (near_plane - far_plane);
  result[matrix_index(3U, 2U)] = -1.0F;
  return result;
}

bool same_transform(const gneiss::transform& left, const gneiss::transform& right) noexcept {
  constexpr float tolerance = 1.0e-5F;
  for (std::size_t index = 0; index < 3U; ++index) {
    if (std::abs(left.translation[index] - right.translation[index]) > tolerance ||
        std::abs(left.scale[index] - right.scale[index]) > tolerance) {
      return false;
    }
  }
  for (std::size_t index = 0; index < 4U; ++index) {
    if (std::abs(left.rotation[index] - right.rotation[index]) > tolerance) {
      return false;
    }
  }
  return true;
}

std::string_view console_severity_name(std::uint32_t severity) noexcept {
  switch (severity) {
  case GNEISS_LOG_TRACE:
    return "TRACE";
  case GNEISS_LOG_DEBUG:
    return "DEBUG";
  case GNEISS_LOG_INFO:
    return "INFO";
  case GNEISS_LOG_WARNING:
    return "WARN";
  case GNEISS_LOG_ERROR:
    return "ERROR";
  case GNEISS_LOG_FATAL:
    return "FATAL";
  default:
    return "UNKNOWN";
  }
}

std::string format_console_entry(const gneiss::editor::console_entry& entry) {
  if (entry.kind == gneiss::editor::console_entry_kind::raw) {
    return std::string{"[RAW] "} + entry.raw_text +
           (entry.was_truncated ? " [line truncated]" : "");
  }
  std::string output{"["};
  output.append(console_severity_name(entry.event.severity));
  output.append("][");
  output.append(entry.event.source);
  output.append("][");
  output.append(entry.event.category);
  output.append("] ");
  output.append(entry.event.message);
  if (entry.event.operation != GNEISS_SUCCESS) {
    output.append(" (result=");
    output.append(std::to_string(entry.event.operation));
    output.push_back(')');
  }
  return output;
}

gneiss::result submit_editor_grid(gneiss_application application) {
  std::vector<gneiss::debug_line> lines;
  lines.reserve(326U);
  constexpr int extent = 80;
  constexpr float spacing = 0.25F;
  for (int index = -extent; index <= extent; ++index) {
    const auto value = static_cast<float>(index) * spacing;
    const auto major = index % 4 == 0;
    const auto color = index == 0 ? IM_COL32(137, 180, 250, 190)
                       : major    ? IM_COL32(166, 173, 200, 105)
                                  : IM_COL32(108, 112, 134, 55);
    lines.push_back({.start = {value, 0.0F, -20.0F},
                     .end = {value, 0.0F, 20.0F},
                     .color_rgba8 = color,
                     .width = major ? 1.25F : 1.0F,
                     .depth_test = 1U,
                     .reserved = {}});
    lines.push_back({.start = {-20.0F, 0.0F, value},
                     .end = {20.0F, 0.0F, value},
                     .color_rgba8 = color,
                     .width = major ? 1.25F : 1.0F,
                     .depth_test = 1U,
                     .reserved = {}});
  }
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
  const auto view = build_view_matrix(state.camera.current_transform());
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
  std::string project;
};

bool parse_options(int argc, char** argv, launch_options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--smoke") {
      options.smoke = true;
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
  operation = gneiss::editor::make_asset_uri(state.asset_root, path, uri);
  if (operation == gneiss::result::success) {
    operation = state.session.save_as(state.asset_root, uri);
  }
  if (operation == gneiss::result::success) {
    state.history.mark_saved();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    const auto uri = std::string{state.session.uri()};
    (void)state.author_assets.acknowledge(uri);
    (void)state.runtime.publish_asset_revision(std::span<const std::string>(&uri, 1U));
#endif
  }
  return operation;
}

gneiss::result save_document(editor_state& state) {
  if (state.session.uri().empty()) {
    return save_document_as(state);
  }
  const auto operation = state.session.save(state.asset_root);
  if (operation == gneiss::result::success) {
    state.history.mark_saved();
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
    const auto uri = std::string{state.session.uri()};
    (void)state.author_assets.acknowledge(uri);
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
    operation = gneiss::from_native(gneiss_application_request_exit(application));
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

void draw_prefab_node(editor_state& state, const gneiss::editor::prefab_node_record& node) {
  const auto& nodes = state.session.prefab_nodes();
  const auto has_children = std::ranges::any_of(
      nodes, [node_id = node.node](const auto& candidate) { return candidate.parent == node_id; });
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
               ImGuiTreeNodeFlags_FramePadding;
  if (!has_children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (state.session.selection() == node.node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::PushID(node.instance_uuid.c_str());
  ImGui::PushID(node.source_node_uuid.c_str());
  const auto label = node.is_instance_root ? std::string{"[Prefab] "} + node.display_name
                                           : node.display_name + " (read-only)";
  if (node.is_read_only) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  }
  const auto is_open = ImGui::TreeNodeEx(label.c_str(), flags);
  if (node.is_read_only) {
    ImGui::PopStyleColor();
  }
  if (ImGui::IsItemClicked()) {
    (void)state.session.select(node.node);
    state.inspected_runtime_node = {};
    state.inspected_runtime_session = 0U;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s\n%s", node.prefab_uri.c_str(),
                      node.is_read_only ? "Prefab source node (read-only)"
                                        : "Prefab instance root");
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.node) {
        draw_prefab_node(state, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
  ImGui::PopID();
}

void draw_scene_node(editor_state& state, const gneiss::editor::scene_node_record& node) {
  const auto& nodes = state.session.nodes();
  const auto& prefab_nodes = state.session.prefab_nodes();
  const auto target_uuid = node.uuid;
  const auto has_children =
      std::ranges::any_of(
          nodes,
          [node_id = node.node](const auto& candidate) { return candidate.parent == node_id; }) ||
      std::ranges::any_of(prefab_nodes, [node_id = node.node](const auto& candidate) {
        return candidate.parent == node_id;
      });
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
               ImGuiTreeNodeFlags_FramePadding;
  if (!has_children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (state.session.selection() == node.node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::PushID(node.uuid.c_str());
  const auto is_open = ImGui::TreeNodeEx(node.display_name.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    (void)state.session.select(node.node);
    state.inspected_runtime_node = {};
    state.inspected_runtime_session = 0U;
  }
  if (ImGui::BeginPopupContextItem("Node Actions")) {
    if (ImGui::MenuItem("Rename", "F2")) {
      state.pending_hierarchy_action = hierarchy_action::rename;
      state.pending_hierarchy_uuid = node.uuid;
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
      state.pending_hierarchy_action = hierarchy_action::duplicate;
      state.pending_hierarchy_uuid = node.uuid;
    }
    if (ImGui::MenuItem("Delete", "Delete")) {
      state.pending_hierarchy_action = hierarchy_action::remove;
      state.pending_hierarchy_uuid = node.uuid;
    }
    ImGui::Separator();
    ImGui::BeginDisabled(state.session.is_dirty() || state.runtime.is_busy());
    if (ImGui::MenuItem("Create Prefab...")) {
      state.pending_prefab_author_action = prefab_author_action::create;
      state.pending_prefab_root_uuid = node.uuid;
      state.prefab_path_buffer.fill('\0');
      const auto path = std::string{"prefabs/"} + node.uuid + ".prefab.json";
      const auto length = std::min(path.size(), state.prefab_path_buffer.size() - 1U);
      std::ranges::copy_n(path.begin(), length, state.prefab_path_buffer.begin());
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("GNEISS_SCENE_NODE_UUID", node.uuid.data(), node.uuid.size());
    ImGui::TextUnformatted(node.display_name.c_str());
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    bool hierarchy_changed = false;
    if (const auto* payload = ImGui::AcceptDragDropPayload("GNEISS_SCENE_NODE_UUID");
        payload != nullptr) {
      const std::string source_uuid{static_cast<const char*>(payload->Data),
                                    static_cast<std::size_t>(payload->DataSize)};
      hierarchy_changed = reparent_with_history(state, source_uuid, target_uuid);
    }
    ImGui::EndDragDropTarget();
    if (hierarchy_changed) {
      if (has_children && is_open) {
        ImGui::TreePop();
      }
      ImGui::PopID();
      return;
    }
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.node) {
        draw_scene_node(state, child);
      }
    }
    for (const auto& child : prefab_nodes) {
      if (child.parent == node.node) {
        draw_prefab_node(state, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void draw_runtime_scene_node(editor_state& state,
                             const std::vector<gneiss::ipc_inspection_node>& nodes,
                             const gneiss::ipc_inspection_node& node) {
  const auto has_children = std::ranges::any_of(
      nodes, [&](const auto& candidate) { return candidate.parent == node.id; });
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
               ImGuiTreeNodeFlags_FramePadding;
  if (!has_children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (state.inspected_runtime_session == state.runtime.scene_mirror().session_id() &&
      state.inspected_runtime_node == node.id) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::PushID(node.uuid.c_str());
  const auto& label = node.name.empty() ? node.uuid : node.name;
  const auto is_open = ImGui::TreeNodeEx(label.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    state.inspected_runtime_node = node.id;
    state.inspected_runtime_session = state.runtime.scene_mirror().session_id();
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.id) {
        draw_runtime_scene_node(state, nodes, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

const gneiss::ipc_inspection_node* selected_runtime_node(const editor_state& state) noexcept {
  const auto& mirror = state.runtime.scene_mirror();
  if (state.inspected_runtime_session == 0U ||
      state.inspected_runtime_session != mirror.session_id()) {
    return nullptr;
  }
  const auto& nodes = mirror.nodes();
  const auto found =
      std::ranges::find(nodes, state.inspected_runtime_node, &gneiss::ipc_inspection_node::id);
  return found == nodes.end() ? nullptr : &*found;
}

gneiss::editor::runtime_property_key runtime_transform_key(const gneiss::ipc_inspection_node& node,
                                                           gneiss_field_id field_id) {
  gneiss::editor::runtime_property_key key{.object = node.id, .type_id = {}, .field_id = field_id};
  const auto type_id = gneiss_transform_type_id();
  std::ranges::copy(type_id.bytes, key.type_id.begin());
  return key;
}

void draw_runtime_property_status(const gneiss::editor::runtime_property_edit* edit,
                                  const gneiss::ipc_property_value& observed) {
  if (edit == nullptr) {
    return;
  }
  switch (edit->state) {
  case gneiss::editor::runtime_property_edit_state::pending:
    ImGui::TextDisabled("等待 Runtime 确认…");
    break;
  case gneiss::editor::runtime_property_edit_state::applied:
    if (edit->canonical_value.payload != observed.payload) {
      ImGui::TextColored({0.95F, 0.75F, 0.35F, 1.0F}, "已应用，但运行逻辑随后覆盖了该值");
    } else {
      ImGui::TextColored({0.65F, 0.9F, 0.55F, 1.0F}, "已由 Runtime 应用");
    }
    break;
  case gneiss::editor::runtime_property_edit_state::rejected:
    ImGui::TextColored({0.95F, 0.45F, 0.45F, 1.0F}, "Runtime 拒绝：%s", edit->message.c_str());
    break;
  case gneiss::editor::runtime_property_edit_state::timed_out:
    ImGui::TextColored({0.95F, 0.75F, 0.35F, 1.0F}, "等待 Runtime 响应超时");
    break;
  case gneiss::editor::runtime_property_edit_state::disconnected:
    ImGui::TextDisabled("Runtime 连接已断开");
    break;
  }
}

void draw_runtime_inspector(editor_state& state, const gneiss::ipc_inspection_node& node) {
  ImGui::Text("Name: %s", node.name.empty() ? node.uuid.c_str() : node.name.c_str());
  ImGui::Text("UUID: %s", node.uuid.c_str());
  const auto editable = state.runtime.supports_property_editing();
  ImGui::TextDisabled(editable ? "Runtime 实时属性" : "Runtime 只读（未协商属性编辑能力）");
  const auto can_apply = !node.uuid.empty() && state.session.find_node(node.uuid) != nullptr;
  ImGui::BeginDisabled(!can_apply);
  if (ImGui::Button("应用 Transform 到作者场景")) {
    state.history_error =
        gneiss::editor::apply_runtime_transform_to_author(state.session, state.history, node);
    if (state.history_error == gneiss::result::success) {
      synchronize_history_dirty(state);
    }
  }
  ImGui::EndDisabled();
  if (!can_apply && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("该 Runtime 节点没有可用的作者场景 UUID 映射");
  }
  ImGui::Separator();
  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto translation = std::to_array(node.local_transform.translation);
    auto scale = std::to_array(node.local_transform.scale);
    gneiss_property_quaternion quaternion{
        node.local_transform.rotation[0], node.local_transform.rotation[1],
        node.local_transform.rotation[2], node.local_transform.rotation[3]};
    std::array<float, 3> rotation{};
    (void)gneiss::editor::quaternion_to_euler_degrees(quaternion, rotation);

    const auto draw_vec3 = [&](const char* label, gneiss_field_id field_id,
                               std::array<float, 3>& value, float speed) {
      const auto observed = value;
      const auto key = runtime_transform_key(node, field_id);
      const auto* edit = state.runtime.property_edit(key);
      const auto pending =
          edit != nullptr && edit->state == gneiss::editor::runtime_property_edit_state::pending;
      ImGui::BeginDisabled(!editable || pending);
      ImGui::PushID(static_cast<int>(field_id));
      (void)ImGui::DragFloat3(label, value.data(), speed);
      const auto committed = ImGui::IsItemDeactivatedAfterEdit();
      ImGui::PopID();
      ImGui::EndDisabled();
      if (committed) {
        const auto revision = edit != nullptr && edit->revision != 0U ? edit->revision : 1U;
        state.runtime_result = state.runtime.request_property_write(key, revision, {value});
        state.runtime_attempted = true;
      }
      draw_runtime_property_status(edit, {observed});
    };

    const auto translation_key = runtime_transform_key(node, GNEISS_TRANSFORM_FIELD_TRANSLATION);
    const auto* translation_edit = state.runtime.property_edit(translation_key);
    ImGui::BeginDisabled(!editable || (translation_edit != nullptr &&
                                       translation_edit->state ==
                                           gneiss::editor::runtime_property_edit_state::pending));
    ImGui::PushID(static_cast<int>(GNEISS_TRANSFORM_FIELD_TRANSLATION));
    (void)ImGui::DragFloat3("Translation", translation.data(), 0.05F);
    const auto translation_committed = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    ImGui::EndDisabled();
    if (translation_committed) {
      const auto revision = translation_edit != nullptr && translation_edit->revision != 0U
                                ? translation_edit->revision
                                : 1U;
      state.runtime_result =
          state.runtime.request_property_write(translation_key, revision, {translation});
      state.runtime_attempted = true;
    }
    draw_runtime_property_status(translation_edit,
                                 {std::to_array(node.local_transform.translation)});

    const auto rotation_key = runtime_transform_key(node, GNEISS_TRANSFORM_FIELD_ROTATION);
    const auto* rotation_edit = state.runtime.property_edit(rotation_key);
    ImGui::BeginDisabled(!editable || (rotation_edit != nullptr &&
                                       rotation_edit->state ==
                                           gneiss::editor::runtime_property_edit_state::pending));
    ImGui::PushID(static_cast<int>(GNEISS_TRANSFORM_FIELD_ROTATION));
    (void)ImGui::DragFloat3("Rotation (degrees)", rotation.data(), 0.25F, 0.0F, 0.0F, "%.1f°");
    const auto rotation_committed = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    ImGui::EndDisabled();
    if (rotation_committed) {
      gneiss_property_quaternion edited{};
      const auto converted = gneiss::editor::euler_degrees_to_quaternion(rotation, edited);
      if (converted == gneiss::result::success) {
        const auto revision = rotation_edit != nullptr && rotation_edit->revision != 0U
                                  ? rotation_edit->revision
                                  : 1U;
        state.runtime_result = state.runtime.request_property_write(
            rotation_key, revision, {std::array<float, 4>{edited.x, edited.y, edited.z, edited.w}});
      } else {
        state.runtime_result = converted;
      }
      state.runtime_attempted = true;
    }
    draw_runtime_property_status(rotation_edit, {std::array<float, 4>{quaternion.x, quaternion.y,
                                                                      quaternion.z, quaternion.w}});

    draw_vec3("Scale", GNEISS_TRANSFORM_FIELD_SCALE, scale, 0.05F);
  }
  ImGui::BeginDisabled();
  if ((node.component_flags & GNEISS_SCENE_NODE_COMPONENT_CAMERA) != 0U &&
      ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto field_of_view = node.camera.vertical_field_of_view_radians;
    auto near_plane = node.camera.near_plane;
    auto far_plane = node.camera.far_plane;
    ImGui::PushID(static_cast<int>(GNEISS_CAMERA_FIELD_VERTICAL_FIELD_OF_VIEW_RADIANS));
    ImGui::DragFloat("Vertical FOV (radians)", &field_of_view);
    ImGui::PopID();
    ImGui::PushID(static_cast<int>(GNEISS_CAMERA_FIELD_NEAR_PLANE));
    ImGui::DragFloat("Near plane", &near_plane);
    ImGui::PopID();
    ImGui::PushID(static_cast<int>(GNEISS_CAMERA_FIELD_FAR_PLANE));
    ImGui::DragFloat("Far plane", &far_plane);
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  if ((node.component_flags & GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER) != 0U &&
      ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("Mesh: %s", node.mesh_uri.empty() ? "(none)" : node.mesh_uri.c_str());
    ImGui::TextWrapped("Material: %s",
                       node.material_uri.empty() ? "(none)" : node.material_uri.c_str());
  }
}

bool draw_property(editor_state& state, const gneiss::editor::inspector_component& component,
                   const gneiss::editor::inspector_property& property, gneiss::result& error) {
  auto value = property.value;
  const auto writable = (property.capabilities & GNEISS_PROPERTY_CAPABILITY_WRITABLE) != 0U;
  bool changed = false;
  ImGui::PushID(static_cast<int>(property.id));
  ImGui::BeginDisabled(!writable);
  switch (property.kind) {
  case GNEISS_PROPERTY_KIND_BOOL: {
    auto checked = value.payload.bool_value != 0U;
    changed = ImGui::Checkbox(property.name.c_str(), &checked);
    value.payload.bool_value = checked ? 1U : 0U;
    break;
  }
  case GNEISS_PROPERTY_KIND_FLOAT32:
    changed = ImGui::DragFloat(property.name.c_str(), &value.payload.float32_value, 0.01F);
    break;
  case GNEISS_PROPERTY_KIND_VEC3:
    changed = ImGui::DragFloat3(property.name.c_str(), &value.payload.vec3_value.x, 0.05F);
    break;
  case GNEISS_PROPERTY_KIND_QUATERNION: {
    std::array<float, 3> euler{};
    error = gneiss::editor::quaternion_to_euler_degrees(value.payload.quaternion_value, euler);
    if (error != gneiss::result::success) {
      break;
    }
    changed = ImGui::DragFloat3(property.name.c_str(), euler.data(), 0.25F, 0.0F, 0.0F, "%.1f°");
    if (changed) {
      error = gneiss::editor::euler_degrees_to_quaternion(euler, value.payload.quaternion_value);
    }
    break;
  }
  default:
    ImGui::TextDisabled("%s: unsupported property kind", property.name.c_str());
    break;
  }
  ImGui::EndDisabled();
  const auto item_activated = ImGui::IsItemActivated();
  ImGui::PopID();
  if (item_activated) {
    ++state.property_edit_serial;
  }
  if (!changed) {
    return false;
  }
  if (error != gneiss::result::success) {
    return false;
  }
  const auto previous = property.value;
  const auto* selected = state.session.selected_node();
  if (selected == nullptr) {
    error = gneiss::result::invalid_state;
    return false;
  }
  const auto uuid = selected->uuid;
  error = state.inspector.set_value(component.type_id, property.id, value);
  if (error != gneiss::result::success) {
    return false;
  }
  const auto type_id = component.type_id;
  const auto field_id = property.id;
  std::string merge_key = "property:" + uuid;
  merge_key.append(reinterpret_cast<const char*>(type_id.bytes), sizeof(type_id.bytes));
  merge_key.append(reinterpret_cast<const char*>(&field_id), sizeof(field_id));
  merge_key.append(reinterpret_cast<const char*>(&state.property_edit_serial),
                   sizeof(state.property_edit_serial));
  const auto record_result = state.history.record(
      {.label = std::string{"修改 "} + property.name,
       .undo =
           [&state, uuid, type_id, field_id, previous] {
             const auto* current = state.session.find_node(uuid);
             if (current == nullptr) {
               return gneiss::result::not_found;
             }
             const auto operation = state.inspector.set_value(state.world, current->entity, type_id,
                                                              field_id, previous);
             if (operation == gneiss::result::success) {
               state.session.mark_dirty();
             }
             return operation;
           },
       .redo =
           [&state, uuid, type_id, field_id, value] {
             const auto* current = state.session.find_node(uuid);
             if (current == nullptr) {
               return gneiss::result::not_found;
             }
             const auto operation =
                 state.inspector.set_value(state.world, current->entity, type_id, field_id, value);
             if (operation == gneiss::result::success) {
               state.session.mark_dirty();
             }
             return operation;
           },
       .merge_key = std::move(merge_key)});
  if (record_result != gneiss::result::success) {
    (void)state.inspector.set_value(type_id, field_id, previous);
    error = record_result;
    return false;
  }
  return true;
}

void draw_reflected_properties(editor_state& state) {
  bool edited = false;
  for (const auto& component : state.inspector.components()) {
    if (!ImGui::CollapsingHeader(component.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      continue;
    }
    for (const auto& property : component.properties) {
      edited = draw_property(state, component, property, state.inspector_error) || edited;
    }
  }
  if (edited) {
    state.session.mark_dirty();
  }
  if (state.inspector_error != gneiss::result::success) {
    const auto message = state.inspector_error.message();
    ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                       static_cast<int>(message.size()), message.data());
  }
}

#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
[[nodiscard]] const char* asset_kind_name(gneiss::editor::asset_browser_kind kind) {
  switch (kind) {
  case gneiss::editor::asset_browser_kind::source:
    return "SRC";
  case gneiss::editor::asset_browser_kind::authored_asset:
    return "ASSET";
  case gneiss::editor::asset_browser_kind::imported_output:
    return "GEN";
  }
  return "?";
}

[[nodiscard]] const char* asset_status_name(gneiss::editor::asset_browser_status status) {
  switch (status) {
  case gneiss::editor::asset_browser_status::untracked:
    return "Untracked";
  case gneiss::editor::asset_browser_status::ready:
    return "Ready";
  case gneiss::editor::asset_browser_status::stale:
    return "Stale";
  case gneiss::editor::asset_browser_status::missing:
    return "Missing";
  }
  return "Unknown";
}

[[nodiscard]] bool is_mesh_asset(const gneiss::editor::asset_browser_entry& entry) {
  return entry.asset_uri.ends_with(".gneiss-mesh") || entry.asset_uri.ends_with(".mesh.json");
}

[[nodiscard]] bool is_material_asset(const gneiss::editor::asset_browser_entry& entry) {
  return entry.asset_uri.ends_with(".material.json");
}

[[nodiscard]] bool is_prefab_asset(const gneiss::editor::asset_browser_entry& entry) {
  return entry.asset_uri.ends_with(".prefab.json");
}

[[nodiscard]] const gneiss::editor::asset_browser_entry*
find_material_for_mesh(const std::vector<gneiss::editor::asset_browser_entry>& entries,
                       const gneiss::editor::asset_browser_entry& mesh) {
  const auto models = mesh.asset_uri.find("/models/");
  const auto prefix =
      models == std::string::npos ? std::string{} : mesh.asset_uri.substr(0U, models);
  const auto preferred = prefix + "/materials/material-0.material.json";
  const auto exact =
      std::ranges::find(entries, preferred, &gneiss::editor::asset_browser_entry::asset_uri);
  if (exact != entries.end()) {
    return &*exact;
  }
  const auto found = std::ranges::find_if(entries, [&prefix](const auto& entry) {
    return is_material_asset(entry) &&
           (prefix.empty() || entry.asset_uri.starts_with(prefix + "/materials/"));
  });
  return found == entries.end() ? nullptr : &*found;
}

void draw_asset_browser(editor_state& state) {
  ImGui::SetNextWindowSizeConstraints(ImVec2(220.0F, 160.0F), ImVec2(FLT_MAX, FLT_MAX));
  ImGui::Begin("Asset Browser", &state.panel_visibility.asset_browser);
  if (ImGui::Button("Refresh")) {
    state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
  }
  ImGui::SameLine();
  if (ImGui::Button("Import...")) {
    std::filesystem::path selected;
    const auto selected_result = gneiss::editor::select_source_asset(selected);
    if (selected_result == gneiss::result::success) {
      state.last_import =
          gneiss::editor::import_external_asset(state.project_root, state.asset_root, selected);
      state.import_attempted = true;
      if (state.last_import.result == gneiss::editor::editor_import_result::success) {
        (void)state.runtime.publish_asset_revision(state.last_import.import.output_uris);
      }
      state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
    } else if (selected_result != gneiss::result::not_ready) {
      state.last_import = {};
      state.last_import.result = gneiss::editor::editor_import_result::io_error;
      state.last_import.diagnostic = std::string{selected_result.message()};
      state.import_attempted = true;
    }
  }
  const auto selected_entry = std::ranges::find(state.assets.entries(), state.assets.selection(),
                                                &gneiss::editor::asset_browser_entry::id);
  const auto can_reimport = selected_entry != state.assets.entries().end() &&
                            selected_entry->kind == gneiss::editor::asset_browser_kind::source;
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_reimport);
  const auto reimport_requested = ImGui::Button("Reimport");
  ImGui::EndDisabled();
  if (reimport_requested) {
    state.last_import = gneiss::editor::reimport_source_asset(
        state.project_root, state.asset_root,
        state.project_root / "sources" / utf8_path(selected_entry->relative_path));
    state.import_attempted = true;
    if (state.last_import.result == gneiss::editor::editor_import_result::success) {
      (void)state.runtime.publish_asset_revision(state.last_import.import.output_uris);
    }
    state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
  }
  state.asset_filter.Draw("Filter", -1.0F);
  if (state.asset_result != gneiss::editor::asset_browser_result::success) {
    ImGui::TextColored(gneiss::editor::theme_error_color(), "Refresh failed: %s",
                       state.assets.diagnostic().c_str());
  }
  if (state.import_attempted) {
    if (state.last_import.result == gneiss::editor::editor_import_result::success) {
      ImGui::TextColored(gneiss::editor::theme_success_color(), "Import succeeded");
    } else {
      ImGui::TextColored(gneiss::editor::theme_error_color(), "Import failed: %s",
                         state.last_import.diagnostic.c_str());
    }
  }
  const auto& reload = state.runtime.asset_reload_status();
  if (reload.state != gneiss::editor::runtime_asset_reload_state::idle) {
    const auto color =
        reload.state == gneiss::editor::runtime_asset_reload_state::applied
            ? gneiss::editor::theme_success_color()
        : reload.state == gneiss::editor::runtime_asset_reload_state::failed ||
                reload.state == gneiss::editor::runtime_asset_reload_state::restart_required
            ? gneiss::editor::theme_error_color()
            : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::TextColored(color, "Runtime asset revision %llu: %s",
                       static_cast<unsigned long long>(reload.revision), reload.message.c_str());
  }
  const auto& author_change = state.author_assets.status();
  if (author_change.state != gneiss::editor::author_asset_change_state::idle) {
    const auto color =
        author_change.state == gneiss::editor::author_asset_change_state::applied
            ? gneiss::editor::theme_success_color()
        : author_change.state == gneiss::editor::author_asset_change_state::conflict ||
                author_change.state == gneiss::editor::author_asset_change_state::failed
            ? gneiss::editor::theme_error_color()
            : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::TextColored(color, "%s: %s", author_change.uri.c_str(), author_change.message.c_str());
  }
  const gneiss::editor::scene_node_record* scene_node = state.session.selected_node();
  const auto* paired_material =
      selected_entry != state.assets.entries().end() && is_mesh_asset(*selected_entry)
          ? find_material_for_mesh(state.assets.entries(), *selected_entry)
          : nullptr;
  const auto can_add = selected_entry != state.assets.entries().end() &&
                       is_mesh_asset(*selected_entry) && paired_material != nullptr;
  ImGui::BeginDisabled(!can_add);
  const auto add_requested = ImGui::Button("Add Mesh");
  ImGui::EndDisabled();
  if (add_requested) {
    const auto was_dirty = state.session.is_dirty();
    gneiss::scene_node_id node;
    state.asset_scene_result = state.session.create_mesh_renderer_node(
        selected_entry->display_name, selected_entry->asset_uri, paired_material->asset_uri, node);
    if (state.asset_scene_result == gneiss::result::success) {
      const auto* created = state.session.selected_node();
      const gneiss::editor::scene_node_snapshot snapshot{.uuid = created->uuid,
                                                         .parent_uuid = {},
                                                         .display_name = created->display_name,
                                                         .mesh_uri = created->mesh_uri,
                                                         .material_uri = created->material_uri};
      state.asset_scene_result = state.history.record(
          {.label = "创建 Mesh Renderer 节点",
           .undo =
               [&state, uuid = snapshot.uuid] {
                 const auto* current = state.session.find_node(uuid);
                 if (current == nullptr) {
                   return gneiss::result::not_found;
                 }
                 gneiss::editor::scene_node_snapshot discarded;
                 return state.session.destroy_node(current->node, discarded);
               },
           .redo =
               [&state, snapshot] {
                 gneiss::scene_node_id restored;
                 return state.session.restore_mesh_renderer_node(snapshot, restored);
               },
           .merge_key = {}});
      if (state.asset_scene_result != gneiss::result::success) {
        gneiss::editor::scene_node_snapshot discarded;
        (void)state.session.destroy_node(node, discarded);
        if (!was_dirty) {
          state.session.clear_dirty();
        }
      }
    }
    state.asset_scene_attempted = true;
    scene_node = state.session.selected_node();
  }
  const auto can_add_prefab =
      selected_entry != state.assets.entries().end() &&
      selected_entry->kind == gneiss::editor::asset_browser_kind::authored_asset &&
      is_prefab_asset(*selected_entry);
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_add_prefab);
  const auto add_prefab_requested = ImGui::Button("Add Prefab");
  ImGui::EndDisabled();
  if (add_prefab_requested) {
    gneiss::scene_node_id root;
    state.asset_scene_result = state.session.create_prefab_instance(
        selected_entry->display_name, selected_entry->asset_uri,
        scene_node == nullptr ? gneiss::scene_node_id{} : scene_node->node, root);
    if (state.asset_scene_result == gneiss::result::success) {
      const auto uuid = state.session.selected_prefab_node()->instance_uuid;
      auto snapshot = std::make_shared<gneiss::editor::prefab_instance_snapshot>();
      state.asset_scene_result = state.history.record(
          {.label = "放置 Prefab 实例",
           .undo =
               [&state, uuid, snapshot] {
                 const auto* current = state.session.find_prefab_root(uuid);
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
      if (state.asset_scene_result != gneiss::result::success) {
        const auto* current = state.session.find_prefab_root(uuid);
        if (current != nullptr) {
          gneiss::editor::prefab_instance_snapshot discarded;
          (void)state.session.destroy_prefab_instance(current->node, discarded);
        }
      }
    }
    state.asset_scene_attempted = true;
    scene_node = state.session.selected_node();
  }
  const auto can_apply_mesh = scene_node != nullptr && !scene_node->material_uri.empty() &&
                              selected_entry != state.assets.entries().end() &&
                              is_mesh_asset(*selected_entry);
  const auto can_apply_material = scene_node != nullptr && !scene_node->mesh_uri.empty() &&
                                  selected_entry != state.assets.entries().end() &&
                                  is_material_asset(*selected_entry);
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_apply_mesh && !can_apply_material);
  const auto apply_requested = ImGui::Button("Apply to Node");
  ImGui::EndDisabled();
  if (apply_requested) {
    const auto was_dirty = state.session.is_dirty();
    const auto uuid = scene_node->uuid;
    const auto previous_mesh = scene_node->mesh_uri;
    const auto previous_material = scene_node->material_uri;
    const auto mesh_uri = can_apply_mesh ? selected_entry->asset_uri : scene_node->mesh_uri;
    const auto material_uri =
        can_apply_material ? selected_entry->asset_uri : scene_node->material_uri;
    state.asset_scene_result =
        state.session.set_mesh_renderer(scene_node->node, mesh_uri, material_uri);
    if (state.asset_scene_result == gneiss::result::success) {
      state.asset_scene_result = state.history.record(
          {.label = "替换 Mesh Renderer 资源",
           .undo =
               [&state, uuid, previous_mesh, previous_material] {
                 const auto* current = state.session.find_node(uuid);
                 return current == nullptr ? gneiss::result::not_found
                                           : state.session.set_mesh_renderer(
                                                 current->node, previous_mesh, previous_material);
               },
           .redo =
               [&state, uuid, mesh = std::string{mesh_uri}, material = std::string{material_uri}] {
                 const auto* current = state.session.find_node(uuid);
                 return current == nullptr
                            ? gneiss::result::not_found
                            : state.session.set_mesh_renderer(current->node, mesh, material);
               },
           .merge_key = {}});
      if (state.asset_scene_result != gneiss::result::success) {
        (void)state.session.set_mesh_renderer(scene_node->node, previous_mesh, previous_material);
        if (!was_dirty) {
          state.session.clear_dirty();
        }
      }
    }
    state.asset_scene_attempted = true;
  }
  if (state.asset_scene_attempted && state.asset_scene_result != gneiss::result::success) {
    const auto message = state.asset_scene_result.message();
    ImGui::TextColored(gneiss::editor::theme_error_color(), "Scene edit failed: %.*s",
                       static_cast<int>(message.size()), message.data());
  }
  ImGui::Separator();
  for (const auto& entry : state.assets.entries()) {
    if (!state.asset_filter.PassFilter(entry.relative_path.c_str())) {
      continue;
    }
    ImGui::PushID(entry.id.c_str());
    const auto label = std::string{"["} + asset_kind_name(entry.kind) + "] " + entry.display_name;
    if (ImGui::Selectable(label.c_str(), state.assets.selection() == entry.id)) {
      (void)state.assets.select(entry.id);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s\n%s", entry.relative_path.c_str(), asset_status_name(entry.status));
    }
    ImGui::PopID();
  }
  ImGui::End();
}
#endif

bool draw_transform_gizmo(editor_state& state, const ImVec2& minimum, const ImVec2& size) noexcept {
  const auto* selected = state.session.selected_node();
  const auto* prefab = state.session.selected_prefab_node();
  const auto editable_prefab = prefab != nullptr && !prefab->is_instance_root;
  if ((selected == nullptr && !editable_prefab) || size.x <= 1.0F || size.y <= 1.0F) {
    state.gizmo_using = false;
    state.gizmo_uuid.clear();
    state.gizmo_instance_uuid.clear();
    state.gizmo_source_uuid.clear();
    return false;
  }

  const auto node = selected != nullptr ? selected->node : prefab->node;
  const auto parent_node = selected != nullptr ? selected->parent : prefab->parent;
  const auto local_transform =
      selected != nullptr ? selected->local_transform : prefab->local_transform;

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
  auto view = build_view_matrix(state.camera.current_transform());
  auto projection = build_gizmo_projection_matrix(viewport->Size.x / viewport->Size.y);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
  ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);
  ImGui::GetWindowDrawList()->AddText(
      ImVec2(minimum.x + 8.0F, minimum.y + size.y - ImGui::GetTextLineHeight() - 8.0F),
      IM_COL32(205, 214, 244, 210), "Grid: 1 unit = 1 m | minor: 0.25 m");
  const auto native_operation = state.gizmo_mode == gizmo_operation::translate ? ImGuizmo::TRANSLATE
                                : state.gizmo_mode == gizmo_operation::rotate  ? ImGuizmo::ROTATE
                                                                               : ImGuizmo::SCALE;
  const auto manipulated = ImGuizmo::Manipulate(view.data(), projection.data(), native_operation,
                                                ImGuizmo::WORLD, model.data());
  const auto using_now = ImGuizmo::IsUsing();
  if (using_now && !state.gizmo_using) {
    state.gizmo_initial_local = local_transform;
    state.gizmo_uuid = selected != nullptr ? selected->uuid : std::string{};
    state.gizmo_instance_uuid = editable_prefab ? prefab->instance_uuid : std::string{};
    state.gizmo_source_uuid = editable_prefab ? prefab->source_node_uuid : std::string{};
    state.gizmo_was_dirty = state.session.is_dirty();
  }
  const auto same_target = selected != nullptr
                               ? state.gizmo_uuid == selected->uuid
                               : state.gizmo_instance_uuid == prefab->instance_uuid &&
                                     state.gizmo_source_uuid == prefab->source_node_uuid;
  if (manipulated && same_target) {
    gneiss::transform target_world = GNEISS_TRANSFORM_IDENTITY;
    operation = gneiss::editor::gizmo_matrix_to_transform(model, target_world);
    gneiss::transform parent_world = GNEISS_TRANSFORM_IDENTITY;
    const gneiss::transform* parent = nullptr;
    if (operation == gneiss::result::success && parent_node.is_valid()) {
      operation = gneiss::from_native(
          gneiss_scene_node_get_world_transform(state.world, parent_node.get(), &parent_world));
      parent = &parent_world;
    }
    gneiss::transform local = GNEISS_TRANSFORM_IDENTITY;
    if (operation == gneiss::result::success) {
      operation = gneiss::editor::world_to_local_transform(parent, target_world, local);
    }
    if (operation == gneiss::result::success) {
      operation = state.session.set_local_transform(node, local);
    }
    state.history_error = operation;
  }
  if (state.gizmo_using && !using_now &&
      (!state.gizmo_uuid.empty() || !state.gizmo_source_uuid.empty())) {
    const auto* current =
        !state.gizmo_uuid.empty() ? state.session.find_node(state.gizmo_uuid) : nullptr;
    const auto* current_prefab =
        !state.gizmo_source_uuid.empty()
            ? state.session.find_prefab_source(state.gizmo_instance_uuid, state.gizmo_source_uuid)
            : nullptr;
    const auto current_transform =
        current != nullptr
            ? &current->local_transform
            : (current_prefab != nullptr ? &current_prefab->local_transform : nullptr);
    if (current_transform != nullptr &&
        !same_transform(state.gizmo_initial_local, *current_transform)) {
      const auto uuid = state.gizmo_uuid;
      const auto instance_uuid = state.gizmo_instance_uuid;
      const auto source_uuid = state.gizmo_source_uuid;
      const auto before = state.gizmo_initial_local;
      const auto after = *current_transform;
      state.history_error = state.history.record(
          {.label = "变换节点",
           .undo =
               [&state, uuid, instance_uuid, source_uuid, before] {
                 const auto* node = !uuid.empty() ? state.session.find_node(uuid) : nullptr;
                 const auto* prefab_node =
                     !source_uuid.empty()
                         ? state.session.find_prefab_source(instance_uuid, source_uuid)
                         : nullptr;
                 const auto target =
                     node != nullptr
                         ? node->node
                         : (prefab_node != nullptr ? prefab_node->node : gneiss::scene_node_id{});
                 return !target.is_valid() ? gneiss::result::not_found
                                           : state.session.set_local_transform(target, before);
               },
           .redo =
               [&state, uuid, instance_uuid, source_uuid, after] {
                 const auto* node = !uuid.empty() ? state.session.find_node(uuid) : nullptr;
                 const auto* prefab_node =
                     !source_uuid.empty()
                         ? state.session.find_prefab_source(instance_uuid, source_uuid)
                         : nullptr;
                 const auto target =
                     node != nullptr
                         ? node->node
                         : (prefab_node != nullptr ? prefab_node->node : gneiss::scene_node_id{});
                 return !target.is_valid() ? gneiss::result::not_found
                                           : state.session.set_local_transform(target, after);
               },
           .merge_key = {}});
      if (state.history_error != gneiss::result::success && current_transform != nullptr) {
        (void)state.session.set_local_transform(node, before);
        if (!state.gizmo_was_dirty) {
          state.session.clear_dirty();
        }
      }
    }
    state.gizmo_uuid.clear();
    state.gizmo_instance_uuid.clear();
    state.gizmo_source_uuid.clear();
  }
  state.gizmo_using = using_now;
  return using_now || ImGuizmo::IsOver();
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
    std::vector<gneiss::editor::asset_file_event> file_events;
    (void)state.asset_watcher.poll_events(file_events);
    for (const auto& event : file_events) {
      if (event.kind != gneiss::editor::asset_file_event_kind::error) {
        (void)state.asset_reimports.notify(event.relative_path);
      }
    }
    std::vector<gneiss::editor::asset_file_event> author_events;
    (void)state.author_asset_watcher.poll_events(author_events);
    for (const auto& event : author_events) {
      if (event.kind == gneiss::editor::asset_file_event_kind::error) {
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
        state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
      } else {
        state.author_assets.mark_failed(change.uri, operation);
      }
    }
    (void)state.asset_reimports.tick(state.project_root, state.asset_root);
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
      if (event.state == gneiss::editor::asset_reimport_state::succeeded ||
          event.state == gneiss::editor::asset_reimport_state::removed) {
        state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
      }
    }
#endif
    if (state.runtime_attempted) {
      state.runtime_result = state.runtime.last_result();
    }
    auto result = state.ui.begin_frame(application, *time);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    ImGuizmo::BeginFrame();
    const auto grid_result = submit_editor_grid(application);
    if (grid_result != gneiss::result::success) {
      return gneiss::to_native(grid_result);
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
        const auto exit_requested = ImGui::MenuItem("Exit");
        if (save_requested) {
          state.save_result = save_document(state);
          state.save_attempted = state.save_result != gneiss::result::not_ready;
        }
        if (save_as_requested) {
          state.save_result = save_document_as(state);
          state.save_attempted = state.save_result != gneiss::result::not_ready;
        }
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

    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0F, 160.0F), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Console", &state.panel_visibility.console)) {
      if (state.runtime.is_building()) {
        ImGui::TextColored(gneiss::editor::theme_warning_color(), "Building game module");
      } else if (state.runtime.is_running()) {
        ImGui::TextColored(gneiss::editor::theme_success_color(), "Running");
      } else if (state.runtime.has_started()) {
        ImGui::Text("Exited with code %d", state.runtime.exit_code());
      } else {
        ImGui::TextDisabled("Runtime has not started");
      }
      ImGui::SameLine();
      if (ImGui::Button("Clear")) {
        state.runtime.clear_output();
      }
      ImGui::SameLine();
      if (ImGui::Button(state.console_paused ? "Resume" : "Pause")) {
        state.console_paused = !state.console_paused;
        const auto& entries = state.runtime.console().entries();
        state.console_pause_entry_id =
            state.console_paused && !entries.empty() ? entries.back().id : 0U;
      }
      ImGui::SameLine();
      ImGui::Checkbox("Auto-scroll", &state.console_auto_scroll);
      if (state.runtime_attempted && state.runtime_result != gneiss::result::success) {
        const auto message = state.runtime_result.message();
        ImGui::TextColored(gneiss::editor::theme_error_color(), "Runtime error: %.*s",
                           static_cast<int>(message.size()), message.data());
      }
      if (!state.runtime.log_file().empty()) {
        const auto log_file = state.runtime.log_file().generic_string();
        ImGui::TextDisabled("Log: %s", log_file.c_str());
      }

      auto severity_toggle = [&state](const char* label, std::uint32_t severity) {
        auto enabled = (state.console_filter.severity_mask & (UINT32_C(1) << severity)) != 0U;
        if (ImGui::Checkbox(label, &enabled)) {
          if (enabled) {
            state.console_filter.severity_mask |= UINT32_C(1) << severity;
          } else {
            state.console_filter.severity_mask &= ~(UINT32_C(1) << severity);
          }
        }
      };
      severity_toggle("Trace", GNEISS_LOG_TRACE);
      ImGui::SameLine();
      severity_toggle("Debug", GNEISS_LOG_DEBUG);
      ImGui::SameLine();
      severity_toggle("Info", GNEISS_LOG_INFO);
      ImGui::SameLine();
      severity_toggle("Warn", GNEISS_LOG_WARNING);
      ImGui::SameLine();
      severity_toggle("Error", GNEISS_LOG_ERROR);
      ImGui::SameLine();
      severity_toggle("Fatal", GNEISS_LOG_FATAL);
      ImGui::SameLine();
      ImGui::Checkbox("Raw", &state.console_filter.include_raw);
      ImGui::SameLine();
      ImGui::Checkbox("Current session", &state.console_filter.current_session_only);

      ImGui::SetNextItemWidth(220.0F);
      ImGui::InputTextWithHint("##ConsoleSearch", "Search", state.console_search.data(),
                               state.console_search.size());
      ImGui::SameLine();
      ImGui::SetNextItemWidth(150.0F);
      ImGui::InputTextWithHint("##ConsoleSource", "Source (exact)", state.console_source.data(),
                               state.console_source.size());
      ImGui::SameLine();
      ImGui::SetNextItemWidth(150.0F);
      ImGui::InputTextWithHint("##ConsoleCategory", "Category (exact)",
                               state.console_category.data(), state.console_category.size());
      state.console_filter.search = state.console_search.data();
      state.console_filter.source = state.console_source.data();
      state.console_filter.category = state.console_category.data();

      std::vector<std::size_t> visible;
      const auto filter_result =
          state.runtime.console().visible_indices(state.console_filter, visible);
      if (state.console_paused) {
        std::erase_if(visible, [&state](std::size_t index) {
          return state.runtime.console().entries()[index].id > state.console_pause_entry_id;
        });
      }
      if (filter_result != gneiss::result::success) {
        ImGui::TextColored(gneiss::editor::theme_error_color(), "Console filter failed");
      }
      ImGui::TextDisabled("Visible: %zu / %zu | Dropped: %llu", visible.size(),
                          state.runtime.console().entries().size(),
                          static_cast<unsigned long long>(state.runtime.console().dropped_count()));
      ImGui::SameLine();
      if (ImGui::Button("Copy visible")) {
        std::string clipboard;
        for (const auto index : visible) {
          clipboard.append(format_console_entry(state.runtime.console().entries()[index]));
          clipboard.push_back('\n');
        }
        ImGui::SetClipboardText(clipboard.c_str());
      }
      ImGui::Separator();
      ImGui::BeginChild("ConsoleLog", ImVec2(0.0F, 0.0F), true,
                        ImGuiWindowFlags_HorizontalScrollbar);
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(visible.size()));
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          const auto& entry =
              state.runtime.console().entries()[visible[static_cast<std::size_t>(row)]];
          const auto line = format_console_entry(entry);
          if (entry.kind == gneiss::editor::console_entry_kind::structured &&
              entry.event.severity >= GNEISS_LOG_ERROR) {
            ImGui::TextColored(gneiss::editor::theme_error_color(), "%s", line.c_str());
          } else if (entry.kind == gneiss::editor::console_entry_kind::structured &&
                     entry.event.severity == GNEISS_LOG_WARNING) {
            ImGui::TextColored(gneiss::editor::theme_warning_color(), "%s", line.c_str());
          } else {
            ImGui::TextUnformatted(line.c_str());
          }
        }
      }
      if (state.console_auto_scroll && !state.console_paused) {
        ImGui::SetScrollHereY(1.0F);
      }
      ImGui::EndChild();
    }
    ImGui::End();

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
          for (const auto& node : runtime_nodes) {
            if (!node.parent.is_valid()) {
              draw_runtime_scene_node(state, runtime_nodes, node);
            }
          }
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
      for (const auto& node : state.session.nodes()) {
        if (!node.parent.is_valid()) {
          draw_scene_node(state, node);
        }
      }
      for (const auto& node : state.session.prefab_nodes()) {
        if (!node.parent.is_valid()) {
          draw_prefab_node(state, node);
        }
      }
      ImGui::Separator();
      ImGui::Selectable("Drop here to move to root", false);
      if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("GNEISS_SCENE_NODE_UUID");
            payload != nullptr) {
          const std::string source_uuid{static_cast<const char*>(payload->Data),
                                        static_cast<std::size_t>(payload->DataSize)};
          (void)reparent_with_history(state, source_uuid, {});
        }
        ImGui::EndDragDropTarget();
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
    const auto scene_view_visible = ImGui::Begin("Scene View", &state.panel_visibility.scene_view,
                                                 ImGuiWindowFlags_NoBackground);
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
    ImGui::End();

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
    if (const auto* runtime_selected = selected_runtime_node(state); runtime_selected != nullptr) {
      state.inspector.clear();
      state.inspected_entity = {};
      state.inspector_error = gneiss::result::success;
      draw_runtime_inspector(state, *runtime_selected);
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
          gneiss::transform previous{};
          state.history_error =
              current == nullptr
                  ? gneiss::result::not_found
                  : state.session.restore_prefab_transform_field(current->node, field_id, previous);
          current = state.session.find_prefab_source(instance_uuid, source_uuid);
          if (state.history_error != gneiss::result::success || current == nullptr) {
            return;
          }
          const auto restored = current->local_transform;
          state.history_error = state.history.record(
              {.label = "恢复 Prefab 来源字段",
               .undo =
                   [&state, instance_uuid, source_uuid, previous] {
                     const auto* node =
                         state.session.find_prefab_source(instance_uuid, source_uuid);
                     return node == nullptr
                                ? gneiss::result::not_found
                                : state.session.set_local_transform(node->node, previous);
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
              (void)state.session.set_local_transform(node->node, previous);
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
          gneiss::transform previous{};
          state.history_error = state.session.restore_prefab_transform(current->node, previous);
          current = state.session.find_prefab_source(instance_uuid, source_uuid);
          if (state.history_error == gneiss::result::success && current != nullptr) {
            const auto restored = current->local_transform;
            state.history_error = state.history.record(
                {.label = "恢复 Prefab 来源变换",
                 .undo =
                     [&state, instance_uuid, source_uuid, previous] {
                       const auto* node =
                           state.session.find_prefab_source(instance_uuid, source_uuid);
                       return node == nullptr
                                  ? gneiss::result::not_found
                                  : state.session.set_local_transform(node->node, previous);
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
                (void)state.session.set_local_transform(node->node, previous);
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
    return 1U;
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
  gneiss::application application;
  editor_state state;
  state.asset_root = project.asset_root;
  state.project_root = project.project_root;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
  const auto watcher_result = state.asset_watcher.start(state.project_root / "sources");
  if (watcher_result != gneiss::result::success) {
    const auto message = watcher_result.message();
    std::fprintf(stderr, "Gneiss Editor 资产监听启动失败：结果=%d，消息=%.*s\n",
                 gneiss::to_native(watcher_result), static_cast<int>(message.size()),
                 message.data());
  }
  const auto author_monitor_result = state.author_assets.initialize(state.asset_root);
  if (author_monitor_result != gneiss::result::success) {
    const auto message = author_monitor_result.message();
    std::fprintf(stderr, "Gneiss Editor 作者资产监视初始化失败：结果=%d，消息=%.*s\n",
                 gneiss::to_native(author_monitor_result), static_cast<int>(message.size()),
                 message.data());
  } else {
    const auto author_watcher_result = state.author_asset_watcher.start(state.asset_root);
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

  auto operation = gneiss::application::create(desc, application);
  if (operation != gneiss::result::success) {
    report_startup_failure("Editor Application 创建", operation, path_utf8(project.project_root));
    return 1;
  }
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
