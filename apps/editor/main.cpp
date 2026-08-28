// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_camera.h"
#include "editor_project.h"
#include "editor_session.h"
#include "editor_theme.h"
#include "imgui_adapter.h"
#include "project_manager.h"
#include "property_inspector_model.h"
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
#include "asset_browser_model.h"
#include "asset_import_controller.h"
#include "native_dialog.h"
#endif

#include <gneiss/application.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace {

struct editor_state {
  gneiss::editor::imgui_adapter ui;
  gneiss::editor::editor_camera camera;
  gneiss::editor::editor_session session;
  gneiss::editor::property_inspector_model inspector;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::entity_id inspected_entity;
  gneiss::result inspector_error = gneiss::result::success;
  std::filesystem::path asset_root;
  std::filesystem::path project_root;
  gneiss::result save_result = gneiss::result::success;
  bool save_attempted = false;
  bool show_imgui_demo = false;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  gneiss::editor::asset_browser_model assets;
  gneiss::editor::asset_browser_result asset_result = gneiss::editor::asset_browser_result::success;
  ImGuiTextFilter asset_filter;
  gneiss::editor::editor_import_report last_import;
  bool import_attempted = false;
#endif
};

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

[[nodiscard]] std::filesystem::path utf8_path(std::string_view value) {
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

[[nodiscard]] std::string path_utf8(const std::filesystem::path& value) {
  const auto text = value.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void draw_scene_node(gneiss::editor::editor_session& session,
                     const gneiss::editor::scene_node_record& node) {
  const auto& nodes = session.nodes();
  const auto has_children = std::ranges::any_of(
      nodes, [node_id = node.node](const auto& candidate) { return candidate.parent == node_id; });
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
               ImGuiTreeNodeFlags_FramePadding;
  if (!has_children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (session.selection() == node.node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::PushID(node.uuid.c_str());
  const auto is_open = ImGui::TreeNodeEx(node.display_name.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    (void)session.select(node.node);
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.node) {
        draw_scene_node(session, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

bool draw_property(gneiss::editor::property_inspector_model& inspector,
                   const gneiss::editor::inspector_component& component,
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
  case GNEISS_PROPERTY_KIND_QUATERNION:
    changed = ImGui::DragFloat4(property.name.c_str(), &value.payload.quaternion_value.x, 0.01F);
    break;
  default:
    ImGui::TextDisabled("%s: unsupported property kind", property.name.c_str());
    break;
  }
  ImGui::EndDisabled();
  ImGui::PopID();
  if (!changed) {
    return false;
  }
  error = inspector.set_value(component.type_id, property.id, value);
  return error == gneiss::result::success;
}

void draw_reflected_properties(editor_state& state) {
  bool edited = false;
  for (const auto& component : state.inspector.components()) {
    if (!ImGui::CollapsingHeader(component.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      continue;
    }
    for (const auto& property : component.properties) {
      edited = draw_property(state.inspector, component, property, state.inspector_error) || edited;
    }
  }
  if (edited) {
    state.session.mark_dirty();
  }
  if (state.inspector_error != gneiss::result::success) {
    const auto message = gneiss::result_message(state.inspector_error);
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

void draw_asset_browser(editor_state& state) {
  ImGui::SetNextWindowPos(ImVec2(0.0F, 440.0F));
  ImGui::SetNextWindowSize(ImVec2(250.0F, 280.0F));
  ImGui::Begin("Asset Browser", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
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
      state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
    } else if (selected_result != gneiss::result::not_ready) {
      state.last_import = {};
      state.last_import.result = gneiss::editor::editor_import_result::io_error;
      state.last_import.diagnostic = std::string{gneiss::result_message(selected_result)};
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
    if (const auto* selected = state.session.selected_node(); selected != nullptr) {
      gneiss_transform target = GNEISS_TRANSFORM_IDENTITY;
      const auto result =
          gneiss_scene_node_get_world_transform(state.world, selected->node.get(), &target);
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
    auto result = state.ui.begin_frame(application, *time);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    const auto selection_result = state.session.validate_selection();
    if (selection_result != gneiss::result::success &&
        selection_result != gneiss::result::invalid_handle) {
      return gneiss::to_native(selection_result);
    }

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("Development")) {
        ImGui::MenuItem("ImGui Demo", nullptr, &state.show_imgui_demo);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(ImVec2(0.0F, 20.0F));
    ImGui::SetNextWindowSize(ImVec2(250.0F, 420.0F));
    ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (!state.session.is_open()) {
      ImGui::TextUnformatted("No scene is open");
    } else {
      for (const auto& node : state.session.nodes()) {
        if (!node.parent.is_valid()) {
          draw_scene_node(state.session, node);
        }
      }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(250.0F, 20.0F));
    ImGui::SetNextWindowSize(ImVec2(730.0F, 700.0F));
    ImGui::Begin("Scene View", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoBackground);
    const auto scene_view_hovered = ImGui::IsWindowHovered();
    ImGui::TextUnformatted("Scene View");
    ImGui::TextDisabled("WASD/QE move | RMB look | Wheel dolly | F focus selection");
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
    if (scene_view_hovered) {
      const auto camera_result = update_editor_camera(state, *time);
      if (camera_result != GNEISS_SUCCESS) {
        ImGui::End();
        return camera_result;
      }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(980.0F, 20.0F));
    ImGui::SetNextWindowSize(ImVec2(300.0F, 700.0F));
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::BeginDisabled(!state.session.is_open());
    const auto save_button_pressed = ImGui::Button("Save");
    ImGui::EndDisabled();
    const auto save_requested =
        state.session.is_open() &&
        (save_button_pressed || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)));
    ImGui::SameLine();
    if (!state.session.is_open()) {
      ImGui::TextDisabled("No scene");
    } else if (state.session.is_dirty()) {
      ImGui::TextColored(gneiss::editor::theme_warning_color(), "Modified");
    } else {
      ImGui::TextColored(gneiss::editor::theme_success_color(), "Saved");
    }
    if (save_requested) {
      state.save_result = state.session.save(state.asset_root);
      state.save_attempted = true;
    }
    if (state.save_attempted && state.save_result != gneiss::result::success) {
      const auto message = gneiss::result_message(state.save_result);
      ImGui::TextColored(gneiss::editor::theme_error_color(), "Save failed: %.*s",
                         static_cast<int>(message.size()), message.data());
    }
    ImGui::Separator();
    if (const auto* selected = state.session.selected_node(); selected != nullptr) {
      if (state.inspected_entity != selected->entity) {
        state.inspector_error = state.inspector.refresh(state.world, selected->entity);
        state.inspected_entity = selected->entity;
      }
      ImGui::Text("Name: %s", selected->display_name.c_str());
      ImGui::Text("UUID: %s", selected->uuid.c_str());
      ImGui::Text("Entity: %llu", static_cast<unsigned long long>(selected->entity.get()));
      ImGui::Separator();
      draw_reflected_properties(state);
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

int run_editor(int argc, char** argv) {
  launch_options options;
  if (!parse_options(argc, argv, options)) {
    return 64;
  }
  gneiss::editor::editor_project project;
  if (options.project.empty()) {
    const auto operation = gneiss::editor::run_project_manager(options.smoke, project);
    if (operation == gneiss::result::not_ready) {
      return 0;
    }
    if (operation != gneiss::result::success) {
      return 65;
    }
  } else if (gneiss::editor::load_editor_project(utf8_path(options.project), project) !=
             gneiss::result::success) {
    return 65;
  }
  const auto asset_root_text = path_utf8(project.asset_root);
  if (asset_root_text.size() > std::numeric_limits<std::uint32_t>::max()) {
    return 64;
  }
  gneiss::application application;
  editor_state state;
  state.asset_root = project.asset_root;
  state.project_root = project.project_root;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  state.asset_result = state.assets.refresh(state.project_root, state.asset_root);
#endif
  const auto title = project.name + " - Gneiss Editor";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_editor;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.window_width = 1280;
  desc.window_height = 720;
  desc.window_flags = GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
  desc.asset_root = asset_root_text.c_str();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root_text.size());

  if (gneiss::application::create(desc, application) != gneiss::result::success) {
    return 1;
  }
  if (state.ui.initialize(application.get()) != GNEISS_SUCCESS) {
    return 2;
  }
  if (state.inspector.initialize() != gneiss::result::success ||
      application.get_world(state.world) != gneiss::result::success ||
      state.session.open(application.get(), state.world, project.startup_scene) !=
          gneiss::result::success ||
      state.camera.initialize(state.world) != gneiss::result::success) {
    state.ui.shutdown(application.get());
    state.session.close();
    return 3;
  }
  const auto run_result = application.run(options.smoke ? 3U : 0U);
  state.ui.shutdown(application.get());
  state.camera.shutdown();
  state.session.close();
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
