// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "panels/document_action_panel.hpp"
#include <imgui.h>
namespace gneiss::editor {
void draw_document_action_panel(document_actions& actions,
                                const document_action_callbacks& callbacks) {
  if (!actions.pending() || !callbacks.save || !callbacks.perform)
    return;
  if (!ImGui::IsPopupOpen("Unsaved Changes"))
    ImGui::OpenPopup("Unsaved Changes");
  if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted("The current scene has unsaved changes.");
  if (ImGui::Button("Save")) {
    const auto next = actions.saved(callbacks.save(callbacks.context));
    if (next != document_action::none) {
      callbacks.perform(callbacks.context, next);
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Discard")) {
    const auto next = actions.discard();
    if (next != document_action::none)
      callbacks.perform(callbacks.context, next);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    actions.cancel();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}
}
