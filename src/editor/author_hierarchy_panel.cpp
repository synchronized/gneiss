// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_hierarchy_panel.hpp"

#include <imgui.h>

#include <algorithm>

namespace gneiss::editor {
namespace {

void draw_prefab_node(const author_hierarchy_view& view, author_hierarchy_request& request,
                      const prefab_node_record& node) {
  const auto& nodes = view.prefab_nodes;
  const auto has_children = std::ranges::any_of(
      nodes, [node_id = node.node](const auto& candidate) { return candidate.parent == node_id; });
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
               ImGuiTreeNodeFlags_FramePadding;
  if (!has_children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (view.selection == node.node) {
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
    request = {
        .action = author_hierarchy_action::select,
        .node = node.node,
        .uuid = {},
        .parent_uuid = {},
    };
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s\n%s", node.prefab_uri.c_str(),
                      node.is_read_only ? "Prefab source node (read-only)"
                                        : "Prefab instance root");
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.node) {
        draw_prefab_node(view, request, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
  ImGui::PopID();
}

void draw_node_menu(const author_hierarchy_view& view, author_hierarchy_request& request,
                    const scene_node_record& node) {
  if (ImGui::BeginPopupContextItem("Node Actions")) {
    if (ImGui::MenuItem("Rename", "F2")) {
      request = {
          .action = author_hierarchy_action::rename,
          .node = node.node,
          .uuid = node.uuid,
          .parent_uuid = {},
      };
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
      request = {
          .action = author_hierarchy_action::duplicate,
          .node = node.node,
          .uuid = node.uuid,
          .parent_uuid = {},
      };
    }
    if (ImGui::MenuItem("Delete", "Delete")) {
      request = {
          .action = author_hierarchy_action::remove,
          .node = node.node,
          .uuid = node.uuid,
          .parent_uuid = {},
      };
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!view.can_create_prefab);
    if (ImGui::MenuItem("Create Prefab...")) {
      request = {
          .action = author_hierarchy_action::create_prefab,
          .node = node.node,
          .uuid = node.uuid,
          .parent_uuid = {},
      };
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
}

void draw_scene_node(const author_hierarchy_view& view, author_hierarchy_request& request,
                     const scene_node_record& node) {
  const auto& nodes = view.nodes;
  const auto& prefab_nodes = view.prefab_nodes;
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
  if (view.selection == node.node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::PushID(node.uuid.c_str());
  const auto is_open = ImGui::TreeNodeEx(node.display_name.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    request = {
        .action = author_hierarchy_action::select,
        .node = node.node,
        .uuid = {},
        .parent_uuid = {},
    };
  }
  draw_node_menu(view, request, node);
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("GNEISS_SCENE_NODE_UUID", node.uuid.data(), node.uuid.size());
    ImGui::TextUnformatted(node.display_name.c_str());
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const auto* payload = ImGui::AcceptDragDropPayload("GNEISS_SCENE_NODE_UUID");
        payload != nullptr && payload->Data != nullptr && payload->DataSize > 0) {
      request = {
          .action = author_hierarchy_action::reparent,
          .node = {},
          .uuid =
              {
                  static_cast<const char*>(payload->Data),
                  static_cast<std::size_t>(payload->DataSize),
              },
          .parent_uuid = target_uuid,
      };
    }
    ImGui::EndDragDropTarget();
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.node) {
        draw_scene_node(view, request, child);
      }
    }
    for (const auto& child : prefab_nodes) {
      if (child.parent == node.node) {
        draw_prefab_node(view, request, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

} // namespace

author_hierarchy_request draw_author_hierarchy(const author_hierarchy_view& view) {
  author_hierarchy_request request;
  for (const auto& node : view.nodes) {
    if (!node.parent.is_valid()) {
      draw_scene_node(view, request, node);
    }
  }
  for (const auto& node : view.prefab_nodes) {
    if (!node.parent.is_valid()) {
      draw_prefab_node(view, request, node);
    }
  }
  ImGui::Separator();
  ImGui::Selectable("Drop here to move to root", false);
  if (ImGui::BeginDragDropTarget()) {
    if (const auto* payload = ImGui::AcceptDragDropPayload("GNEISS_SCENE_NODE_UUID");
        payload != nullptr && payload->Data != nullptr && payload->DataSize > 0) {
      request = {
          .action = author_hierarchy_action::reparent,
          .node = {},
          .uuid =
              {
                  static_cast<const char*>(payload->Data),
                  static_cast<std::size_t>(payload->DataSize),
              },
          .parent_uuid = {},
      };
    }
    ImGui::EndDragDropTarget();
  }
  return request;
}

} // namespace gneiss::editor
