// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "panels/runtime_panels.hpp"
#include "engine/core/error.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/world/component_identity.hpp"
#include "panels/reflected_property_editor.hpp"

#include "viewport/editor_rotation_math.hpp"

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
  ImGui::PushID(node.uuid.string().c_str());
  const auto& label = node.name.empty() ? node.uuid.string() : node.name;
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

runtime_property_key runtime_transform_key(const runtime_scene_node& node, std::uint32_t field_id) {
  runtime_property_key key{
      .object = {node.id.value, node.id.generation}, .type_id = {}, .field_id = field_id};
  const auto type_id = gneiss::engine::transform_component_type;
  std::ranges::copy(type_id.bytes(), key.type_id.begin());
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
  ImGui::Text("Name: %s", node.name.empty() ? node.uuid.string().c_str() : node.name.c_str());
  ImGui::Text("UUID: %s", node.uuid.string().c_str());
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
    auto translation = gneiss::engine::math::component_array(node.local_transform.translation);
    auto scale = gneiss::engine::math::component_array(node.local_transform.scale);
    gneiss::engine::math::quaternion quaternion = gneiss::engine::math::quaternion_xyzw(
        node.local_transform.rotation[0], node.local_transform.rotation[1],
        node.local_transform.rotation[2], node.local_transform.rotation[3]);
    std::array<float, 3> rotation{};
    (void)quaternion_to_euler_degrees(quaternion, rotation);

    const auto draw_vec3 = [&](const char* label, std::uint32_t field_id,
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

    const auto translation_key =
        runtime_transform_key(node, gneiss::engine::transform_translation_field.get());
    const auto* translation_edit = actions.find_edit(actions.context, translation_key);
    ImGui::BeginDisabled(!editable ||
                         (translation_edit != nullptr &&
                          translation_edit->state == runtime_property_edit_state::pending));
    ImGui::PushID(static_cast<int>(gneiss::engine::transform_translation_field.get()));
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
    draw_runtime_property_status(translation_edit, {gneiss::engine::math::component_array(
                                                       node.local_transform.translation)});

    const auto rotation_key =
        runtime_transform_key(node, gneiss::engine::transform_rotation_field.get());
    const auto* rotation_edit = actions.find_edit(actions.context, rotation_key);
    ImGui::BeginDisabled(
        !editable ||
        (rotation_edit != nullptr && rotation_edit->state == runtime_property_edit_state::pending));
    ImGui::PushID(static_cast<int>(gneiss::engine::transform_rotation_field.get()));
    (void)ImGui::DragFloat3("Rotation (degrees)", rotation.data(), 0.25F, 0.0F, 0.0F, "%.1f°");
    const auto rotation_committed = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    ImGui::EndDisabled();
    if (rotation_committed) {
      gneiss::engine::math::quaternion edited{};
      const auto converted = euler_degrees_to_quaternion(rotation, edited);
      if (converted == gneiss::engine::error::success) {
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

    draw_vec3("Scale", gneiss::engine::transform_scale_field.get(), scale, 0.05F);
  }
  for (const auto& component : node.components) {
    auto view = component.view();
    if (!view)
      continue;
    auto properties = view->properties();
    auto type = gneiss::engine::meta::describe_type(component.guid());
    if (!properties || !type)
      continue;
    ImGui::PushID(component.guid().string().c_str());
    if (ImGui::CollapsingHeader(std::string(type->name).c_str())) {
      for (const auto& property : *properties) {
        auto child = view->property(property.id);
        if (!child)
          continue;
        auto value = child->read_value();
        runtime_property_value observed;
        bool supported = false;
        if (value) {
          supported = std::visit(
              [&](const auto& payload) {
                using T = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<T, engine::guid>)
                  observed.payload = payload.bytes();
                else if constexpr (std::is_same_v<T, engine::math::vector3>)
                  observed.payload = std::array{payload.x, payload.y, payload.z};
                else if constexpr (std::is_same_v<T, engine::math::quaternion>)
                  observed.payload = std::array{payload.x, payload.y, payload.z, payload.w};
                else if constexpr (std::is_same_v<T, engine::math::vector2> ||
                                   std::is_same_v<T, engine::math::vector4> ||
                                   std::is_same_v<T, std::monostate>)
                  return false;
                else
                  observed.payload = payload;
                return true;
              },
              value->payload);
        }
        runtime_property_key key{
            .object = node.id, .type_id = component.guid().bytes(), .field_id = property.id.get()};
        const auto* pending = actions.find_edit ? actions.find_edit(actions.context, key) : nullptr;
        const bool readonly =
            engine::meta::has_flags(property.flags,
                                    engine::meta::property_flags::editor_read_only) ||
            !engine::meta::has_flags(property.capabilities,
                                     engine::meta::property_capabilities::writable);
        ImGui::BeginDisabled(!editable || readonly || !supported ||
                             (pending && pending->state == runtime_property_edit_state::pending));
        std::uint64_t serial{};
        engine::error status;
        ImGui::PushID(static_cast<int>(property.id.get()));
        if (supported) {
          if (draw_property_value(std::string(property.name).c_str(), *value, child->type(), serial,
                                  status)) {
            // 转换同一种标量，协议不支持的嵌套/容器属性仅展示。
            runtime_property_value input;
            std::visit(
                [&](const auto& payload) {
                  using T = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<T, engine::guid>)
                    input.payload = payload.bytes();
                  else if constexpr (std::is_same_v<T, engine::math::vector3>)
                    input.payload = std::array{payload.x, payload.y, payload.z};
                  else if constexpr (std::is_same_v<T, engine::math::quaternion>)
                    input.payload = std::array{payload.x, payload.y, payload.z, payload.w};
                  else if constexpr (!std::is_same_v<T, engine::math::vector2> &&
                                     !std::is_same_v<T, engine::math::vector4>)
                    input.payload = payload;
                },
                value->payload);
            actions.report_result(
                actions.context,
                actions.write_property(actions.context, key,
                                       pending && pending->revision ? pending->revision : 1U,
                                       std::move(input)));
          }
        } else {
          (void)draw_reflected_properties(*child, std::string(property.name).c_str(), serial,
                                          status);
        }
        ImGui::PopID();
        ImGui::EndDisabled();
        if (supported)
          draw_runtime_property_status(pending, observed);
      }
    }
    ImGui::PopID();
  }
}

} // namespace gneiss::editor
