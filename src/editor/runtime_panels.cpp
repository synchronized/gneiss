// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_panels.hpp"

#include "editor_rotation_math.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace gneiss::editor {
namespace {
void draw_runtime_scene_node(const runtime_scene_mirror& mirror, runtime_scene_selection& selection,
                             const std::vector<runtime_scene_node>& nodes,
                             const runtime_scene_node& node) {
  const auto has_children = std::ranges::any_of(
      nodes, [&](const auto& candidate) { return candidate.parent == node.id; });
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
               ImGuiTreeNodeFlags_FramePadding;
  if (!has_children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (selection.session == mirror.session_id() && selection.object == node.id) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::PushID(node.uuid.c_str());
  const auto& label = node.name.empty() ? node.uuid : node.name;
  const auto is_open = ImGui::TreeNodeEx(label.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    selection.object = node.id;
    selection.session = mirror.session_id();
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.id) {
        draw_runtime_scene_node(mirror, selection, nodes, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

runtime_property_key runtime_transform_key(const runtime_scene_node& node,
                                           gneiss_field_id field_id) {
  runtime_property_key key{
      .object = {node.id.value, node.id.generation}, .type_id = {}, .field_id = field_id};
  const auto type_id = gneiss_transform_type_id();
  std::ranges::copy(type_id.bytes, key.type_id.begin());
  return key;
}

void draw_runtime_property_status(const runtime_property_edit* edit,
                                  const runtime_property_value& observed) {
  if (edit == nullptr) {
    return;
  }
  switch (edit->state) {
  case runtime_property_edit_state::pending:
    ImGui::TextDisabled("等待 Runtime 确认…");
    break;
  case runtime_property_edit_state::applied:
    if (edit->canonical_value.payload != observed.payload) {
      ImGui::TextColored({0.95F, 0.75F, 0.35F, 1.0F}, "已应用，但运行逻辑随后覆盖了该值");
    } else {
      ImGui::TextColored({0.65F, 0.9F, 0.55F, 1.0F}, "已由 Runtime 应用");
    }
    break;
  case runtime_property_edit_state::rejected:
    ImGui::TextColored({0.95F, 0.45F, 0.45F, 1.0F}, "Runtime 拒绝：%s", edit->message.c_str());
    break;
  case runtime_property_edit_state::timed_out:
    ImGui::TextColored({0.95F, 0.75F, 0.35F, 1.0F}, "等待 Runtime 响应超时");
    break;
  case runtime_property_edit_state::disconnected:
    ImGui::TextDisabled("Runtime 连接已断开");
    break;
  }
}

} // namespace

const runtime_scene_node* selected_runtime_node(const runtime_scene_mirror& mirror,
                                                const runtime_scene_selection& selection) noexcept {
  if (selection.session == 0U || selection.session != mirror.session_id()) {
    return nullptr;
  }
  const auto& nodes = mirror.nodes();
  const auto found = std::ranges::find(nodes, selection.object, &runtime_scene_node::id);
  return found == nodes.end() ? nullptr : &*found;
}

void draw_runtime_hierarchy(const runtime_scene_mirror& mirror,
                            runtime_scene_selection& selection) {
  for (const auto& node : mirror.nodes()) {
    if (!node.parent.is_valid()) {
      draw_runtime_scene_node(mirror, selection, mirror.nodes(), node);
    }
  }
}

void draw_runtime_inspector(const runtime_scene_node& node,
                            const runtime_inspector_actions& actions) {
  ImGui::Text("Name: %s", node.name.empty() ? node.uuid.c_str() : node.name.c_str());
  ImGui::Text("UUID: %s", node.uuid.c_str());
  const auto editable = actions.editable;
  ImGui::TextDisabled(editable ? "Runtime 实时属性" : "Runtime 只读（未协商属性编辑能力）");
  const auto can_apply = actions.can_apply_to_author;
  ImGui::BeginDisabled(!can_apply);
  if (ImGui::Button("应用 Transform 到作者场景")) {
    actions.apply_to_author(actions.context, node);
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
    (void)quaternion_to_euler_degrees(quaternion, rotation);

    const auto draw_vec3 = [&](const char* label, gneiss_field_id field_id,
                               std::array<float, 3>& value, float speed) {
      const auto observed = value;
      const auto key = runtime_transform_key(node, field_id);
      const auto* edit = actions.find_edit(actions.context, key);
      const auto pending = edit != nullptr && edit->state == runtime_property_edit_state::pending;
      ImGui::BeginDisabled(!editable || pending);
      ImGui::PushID(static_cast<int>(field_id));
      (void)ImGui::DragFloat3(label, value.data(), speed);
      const auto committed = ImGui::IsItemDeactivatedAfterEdit();
      ImGui::PopID();
      ImGui::EndDisabled();
      if (committed) {
        const auto revision = edit != nullptr && edit->revision != 0U ? edit->revision : 1U;
        actions.report_result(actions.context,
                              actions.write_property(actions.context, key, revision, {value}));
      }
      draw_runtime_property_status(edit, {observed});
    };

    const auto translation_key = runtime_transform_key(node, GNEISS_TRANSFORM_FIELD_TRANSLATION);
    const auto* translation_edit = actions.find_edit(actions.context, translation_key);
    ImGui::BeginDisabled(!editable ||
                         (translation_edit != nullptr &&
                          translation_edit->state == runtime_property_edit_state::pending));
    ImGui::PushID(static_cast<int>(GNEISS_TRANSFORM_FIELD_TRANSLATION));
    (void)ImGui::DragFloat3("Translation", translation.data(), 0.05F);
    const auto translation_committed = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    ImGui::EndDisabled();
    if (translation_committed) {
      const auto revision = translation_edit != nullptr && translation_edit->revision != 0U
                                ? translation_edit->revision
                                : 1U;
      actions.report_result(
          actions.context,
          actions.write_property(actions.context, translation_key, revision, {translation}));
    }
    draw_runtime_property_status(translation_edit,
                                 {std::to_array(node.local_transform.translation)});

    const auto rotation_key = runtime_transform_key(node, GNEISS_TRANSFORM_FIELD_ROTATION);
    const auto* rotation_edit = actions.find_edit(actions.context, rotation_key);
    ImGui::BeginDisabled(
        !editable ||
        (rotation_edit != nullptr && rotation_edit->state == runtime_property_edit_state::pending));
    ImGui::PushID(static_cast<int>(GNEISS_TRANSFORM_FIELD_ROTATION));
    (void)ImGui::DragFloat3("Rotation (degrees)", rotation.data(), 0.25F, 0.0F, 0.0F, "%.1f°");
    const auto rotation_committed = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    ImGui::EndDisabled();
    if (rotation_committed) {
      gneiss_property_quaternion edited{};
      const auto converted = euler_degrees_to_quaternion(rotation, edited);
      if (converted == gneiss::result::success) {
        const auto revision = rotation_edit != nullptr && rotation_edit->revision != 0U
                                  ? rotation_edit->revision
                                  : 1U;
        actions.report_result(
            actions.context,
            actions.write_property(actions.context, rotation_key, revision,
                                   {std::array<float, 4>{edited.x, edited.y, edited.z, edited.w}}));
      } else {
        actions.report_result(actions.context, converted);
      }
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

} // namespace gneiss::editor
