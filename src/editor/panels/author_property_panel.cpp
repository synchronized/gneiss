// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "panels/author_property_panel.hpp"
#include "engine/core/error.hpp"

#include "editor_theme.hpp"
#include "panels/reflected_property_editor.hpp"

#include <imgui.h>

namespace gneiss::editor {
namespace {

bool draw_property(const inspector_component& component, const inspector_property& property,
                   std::uint64_t& edit_serial, gneiss::engine::error& error,
                   const author_property_actions& actions) {
  auto value = property.value;
  if (value.kind() != property.kind) {
    error = gneiss::engine::error::invalid_argument;
    return false;
  }
  const auto writable =
      has_flags(property.capabilities, gneiss::engine::meta::property_capabilities::writable);
  bool changed = false;
  ImGui::PushID(static_cast<int>(property.id.get()));
  ImGui::BeginDisabled(!writable);
  changed = draw_property_value(property.name.c_str(), value, property.choices, edit_serial, error);
  ImGui::EndDisabled();
  ImGui::PopID();
  if (!changed) {
    return false;
  }
  if (error != gneiss::engine::error::success) {
    return false;
  }
  error = actions.write(actions.context, component, property, value, edit_serial);
  return error.ok();
}

} // namespace

void draw_author_properties(std::span<const inspector_component> components,
                            std::uint64_t& edit_serial, gneiss::engine::error& error,
                            const author_property_actions& actions) {
  for (const auto& component : components) {
    if (!ImGui::CollapsingHeader(component.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      continue;
    }
    for (const auto& property : component.properties) {
      (void)draw_property(component, property, edit_serial, error, actions);
    }
  }
  if (error != gneiss::engine::error::success) {
    const auto message = error.message();
    ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                       static_cast<int>(message.size()), message.data());
  }
}

} // namespace gneiss::editor
