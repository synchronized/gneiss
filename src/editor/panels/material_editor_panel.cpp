// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "panels/material_editor_panel.hpp"
#include "panels/reflected_property_editor.hpp"
#include <imgui.h>
#include <optional>
namespace gneiss::editor {
void draw_material_editor(material_editor_model& model, engine::files::service& files,
                          bool& visible, std::uint64_t& serial, engine::error& status,
                          bool& focused) {
  focused = false;
  model.advance();
  if (!visible)
    return;
  bool open = true;
  if (ImGui::Begin("Material Editor", &open)) {
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !model.saving() && model.is_dirty())
        status = model.begin_save(files);
      if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (ImGui::GetIO().KeyShift && model.can_redo())
          status = model.redo();
        else if (!ImGui::GetIO().KeyShift && model.can_undo())
          status = model.undo();
      }
    }
    if (!model.is_open())
      ImGui::TextUnformatted("Select an authored material in Asset Browser.");
    else {
      ImGui::TextWrapped("%.*s%s", static_cast<int>(model.uri().size()), model.uri().data(),
                         model.is_dirty() ? " *" : "");
      ImGui::BeginDisabled(model.saving() || !model.is_dirty());
      if (ImGui::Button("Save Material"))
        status = model.begin_save(files);
      ImGui::SameLine();
      if (ImGui::Button("Discard Changes"))
        ImGui::OpenPopup("Discard material edits?");
      ImGui::EndDisabled();
      if (ImGui::BeginPopupModal("Discard material edits?", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Discard all unsaved changes to this material?");
        if (ImGui::Button("Discard")) {
          status = model.discard();
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::BeginDisabled(!model.can_undo());
      if (ImGui::Button("Undo Material"))
        status = model.undo();
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!model.can_redo());
      if (ImGui::Button("Redo Material"))
        status = model.redo();
      ImGui::EndDisabled();
      if (model.saving())
        ImGui::TextUnformatted("Saving...");
      else if (model.save_status().failed())
        ImGui::TextWrapped("Save failed: %s", model.save_status().message().data());
      ImGui::TextUnformatted("Texture URI: press Enter to apply. No live GPU preview.");
      std::optional<property_edit> pending;
      auto root = engine::meta::instance::from(model.description());
      if (!root)
        status = root.error();
      else {
        pending = draw_reflected_properties(*root, "Material", serial, status);
      }
      // 遍历结束后修改，避免更换快照使借用视图失效。
      if (pending) {
        if (pending->object) {
          auto registry = engine::meta::default_registry();
          auto source = registry ? engine::meta::instance::from_object(
                                       *registry, pending->object_type, pending->object.get())
                                 : engine::result<engine::meta::instance>{registry.error()};
          status = source ? model.replace_object(pending->path, *source) : source.error();
        } else
          status = model.set_value(pending->path, pending->data, serial);
      }
    }
    if (status == engine::error::invalid_state && model.is_dirty())
      ImGui::TextWrapped("Save or discard this material before closing or opening another file.");
    if (status.failed())
      ImGui::TextWrapped("%s", status.message().data());
  }
  ImGui::End();
  if (!open) {
    if (model.is_dirty() || model.saving())
      status = engine::error::invalid_state;
    else
      visible = false;
  }
}
}
