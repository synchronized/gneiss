// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_property_panel.hpp"

#include "editor_rotation_math.hpp"
#include "editor_theme.hpp"

#include <imgui.h>

#include <array>

namespace gneiss::editor {
namespace {

bool draw_property(const inspector_component& component, const inspector_property& property,
                   std::uint64_t& edit_serial, result& error,
                   const author_property_actions& actions) {
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
    ++edit_serial;
  }
  if (!changed) {
    return false;
  }
  if (error != gneiss::result::success) {
    return false;
  }
  error = actions.write(actions.context, component, property, value, edit_serial);
  return error.ok();
}

} // namespace

void draw_author_properties(std::span<const inspector_component> components,
                            std::uint64_t& edit_serial, result& error,
                            const author_property_actions& actions) {
  for (const auto& component : components) {
    if (!ImGui::CollapsingHeader(component.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      continue;
    }
    for (const auto& property : component.properties) {
      (void)draw_property(component, property, edit_serial, error, actions);
    }
  }
  if (error != gneiss::result::success) {
    const auto message = error.message();
    ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                       static_cast<int>(message.size()), message.data());
  }
}

} // namespace gneiss::editor
