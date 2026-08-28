// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_camera.h"
#include "editor_command_history.h"
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
#include <array>
#include <cstdint>
#include <filesystem>
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

struct editor_state {
  gneiss::editor::imgui_adapter ui;
  gneiss::editor::editor_camera camera;
  gneiss::editor::editor_session session;
  gneiss::editor::editor_command_history history;
  gneiss::editor::property_inspector_model inspector;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::entity_id inspected_entity;
  gneiss::result inspector_error = gneiss::result::success;
  gneiss::result history_error = gneiss::result::success;
  std::filesystem::path asset_root;
  std::filesystem::path project_root;
  gneiss::result save_result = gneiss::result::success;
  bool save_attempted = false;
  bool show_imgui_demo = false;
  std::uint64_t property_edit_serial = 0U;
  std::array<char, 128> rename_buffer{};
  std::string rename_uuid;
  std::string rename_previous;
  hierarchy_action pending_hierarchy_action = hierarchy_action::none;
  std::string pending_hierarchy_uuid;
  document_action pending_document_action = document_action::none;
#if defined(GNEISS_EDITOR_HAS_ASSET_BROWSER)
  gneiss::editor::asset_browser_model assets;
  gneiss::editor::asset_browser_result asset_result = gneiss::editor::asset_browser_result::success;
  ImGuiTextFilter asset_filter;
  gneiss::editor::editor_import_report last_import;
  bool import_attempted = false;
  gneiss::result asset_scene_result = gneiss::result::success;
  bool asset_scene_attempted = false;
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
    synchronize_history_dirty(state);
  }
  return result;
}

gneiss::result redo_editor_command(editor_state& state) noexcept {
  const auto result = state.history.redo();
  if (result == gneiss::result::success) {
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
  }
  return operation;
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
           }});
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

void draw_scene_node(editor_state& state, const gneiss::editor::scene_node_record& node) {
  const auto& nodes = state.session.nodes();
  const auto target_uuid = node.uuid;
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
  ImGui::PushID(node.uuid.c_str());
  const auto is_open = ImGui::TreeNodeEx(node.display_name.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    (void)state.session.select(node.node);
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
    ImGui::TreePop();
  }
  ImGui::PopID();
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
  case GNEISS_PROPERTY_KIND_QUATERNION:
    changed = ImGui::DragFloat4(property.name.c_str(), &value.payload.quaternion_value.x, 0.01F);
    break;
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

[[nodiscard]] bool is_mesh_asset(const gneiss::editor::asset_browser_entry& entry) {
  return entry.asset_uri.ends_with(".gneiss-mesh") || entry.asset_uri.ends_with(".mesh.json");
}

[[nodiscard]] bool is_material_asset(const gneiss::editor::asset_browser_entry& entry) {
  return entry.asset_uri.ends_with(".material.json");
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
               }});
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
               }});
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
    const auto message = gneiss::result_message(state.asset_scene_result);
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
      if (ImGui::BeginMenu("Development")) {
        ImGui::MenuItem("ImGui Demo", nullptr, &state.show_imgui_demo);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }
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

    ImGui::SetNextWindowPos(ImVec2(0.0F, 20.0F));
    ImGui::SetNextWindowSize(ImVec2(250.0F, 420.0F));
    ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
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
                     }});
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
                   }});
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
                   }});
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
                   }});
          if (state.history_error != gneiss::result::success) {
            gneiss::scene_node_id restored;
            (void)state.session.restore_subtree(snapshot, restored);
            if (!was_dirty) {
              state.session.clear_dirty();
            }
          }
        }
      }
      for (const auto& node : state.session.nodes()) {
        if (!node.parent.is_valid()) {
          draw_scene_node(state, node);
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
    }
    ImGui::End();

    if (state.history_error != gneiss::result::success &&
        state.history_error != gneiss::result::not_ready) {
      const auto message = gneiss::result_message(state.history_error);
      ImGui::SetNextWindowPos(ImVec2(500.0F, 24.0F));
      ImGui::Begin("Command Error", nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration);
      ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                         static_cast<int>(message.size()), message.data());
      ImGui::End();
    }

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
                     }});
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
                     }});
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
                   }});
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
  desc.close_requested = handle_close_requested;
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
  state.history.clear();
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
