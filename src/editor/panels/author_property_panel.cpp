// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "panels/author_property_panel.hpp"
#include "engine/core/result.hpp"

#include "editor_theme.hpp"
#include "viewport/editor_rotation_math.hpp"

#include <imgui.h>

#include <array>

namespace gneiss::editor {
namespace {

bool draw_property(const inspector_component& component, const inspector_property& property,
                   std::uint64_t& edit_serial, gneiss::engine::result& error,
                   const author_property_actions& actions) {
  auto value = property.value;
  if (value.kind() != property.kind) {
    error = gneiss::engine::result::invalid_argument;
    return false;
  }
  const auto writable =
      has_flags(property.capabilities, gneiss::engine::meta::property_capabilities::writable);
  bool changed = false;
  ImGui::PushID(static_cast<int>(property.id.get()));
  ImGui::BeginDisabled(!writable);
  switch (property.kind) {
  case gneiss::engine::value_kind::boolean: {
    auto checked = std::get<bool>(value.payload);
    changed = ImGui::Checkbox(property.name.c_str(), &checked);
    value.payload = checked;
    break;
  }
  case gneiss::engine::value_kind::float32:
    changed = ImGui::DragFloat(property.name.c_str(), &std::get<float>(value.payload), 0.01F);
    break;
  case gneiss::engine::value_kind::vec3:
    changed = ImGui::DragFloat3(property.name.c_str(),
                                &std::get<gneiss::engine::math::vector3>(value.payload).x, 0.05F);
    break;
  case gneiss::engine::value_kind::quaternion: {
    std::array<float, 3> euler{};
    error = gneiss::editor::quaternion_to_euler_degrees(
        std::get<gneiss::engine::math::quaternion>(value.payload), euler);
    if (error != gneiss::engine::result::success) {
      break;
    }
    changed = ImGui::DragFloat3(property.name.c_str(), euler.data(), 0.25F, 0.0F, 0.0F, "%.1f°");
    if (changed) {
      error = gneiss::editor::euler_degrees_to_quaternion(
          euler, std::get<gneiss::engine::math::quaternion>(value.payload));
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
  if (error != gneiss::engine::result::success) {
    return false;
  }
  error = actions.write(actions.context, component, property, value, edit_serial);
  return error.ok();
}

} // namespace

void draw_author_properties(std::span<const inspector_component> components,
                            std::uint64_t& edit_serial, gneiss::engine::result& error,
                            const author_property_actions& actions) {
  for (const auto& component : components) {
    if (!ImGui::CollapsingHeader(component.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      continue;
    }
    for (const auto& property : component.properties) {
      (void)draw_property(component, property, edit_serial, error, actions);
    }
  }
  if (error != gneiss::engine::result::success) {
    const auto message = error.message();
    ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                       static_cast<int>(message.size()), message.data());
  }
}

} // namespace gneiss::editor
