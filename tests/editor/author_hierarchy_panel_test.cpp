// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_hierarchy_panel.hpp"

#include <imgui.h>

#include <array>

int main() try {
  using namespace gneiss::editor;
  std::array<scene_node_record, 2> nodes;
  nodes[0].node = gneiss::scene_node_id{1U};
  nodes[0].uuid = "node-a";
  nodes[0].display_name = "First";
  nodes[1].node = gneiss::scene_node_id{2U};
  nodes[1].uuid = "node-b";
  nodes[1].display_name = "Second";
  const author_hierarchy_view view{
      .nodes = nodes,
      .prefab_nodes = {},
      .selection = {},
      .can_create_prefab = true,
  };
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1024.0F, 768.0F};
  io.DeltaTime = 1.0F / 60.0F;
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  const auto draw = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0F, 0.0F});
    ImGui::SetNextWindowSize({700.0F, 500.0F});
    ImGui::Begin("Author Hierarchy");
    auto request = draw_author_hierarchy(view);
    ImGui::End();
    ImGui::Render();
    return request;
  };
  if (draw().action != author_hierarchy_action::none) {
    ImGui::DestroyContext();
    return 1;
  }
  const auto& style = ImGui::GetStyle();
  const float x = style.WindowPadding.x + ImGui::GetTreeNodeToLabelSpacing() + 10.0F;
  const float y =
      ImGui::GetFrameHeight() + style.WindowPadding.y + (ImGui::GetFrameHeight() * 0.5F);
  io.AddMousePosEvent(x, y);
  (void)draw();
  io.AddMouseButtonEvent(0, true);
  const auto selected = draw();
  if (selected.action != author_hierarchy_action::select || selected.node != nodes[0].node) {
    ImGui::DestroyContext();
    return 2;
  }
  io.AddMousePosEvent(x, y + ImGui::GetFrameHeight() + style.ItemSpacing.y);
  (void)draw();
  (void)draw();
  io.AddMouseButtonEvent(0, false);
  const auto request = draw();
  // UI 只复制身份并返回请求，递归绘制时没有修改父子关系。
  if (request.action != author_hierarchy_action::reparent || request.uuid != "node-a" ||
      request.parent_uuid != "node-b" || nodes[0].parent.is_valid() || nodes[1].parent.is_valid()) {
    ImGui::DestroyContext();
    return 3;
  }
  nodes[0].uuid.clear();
  nodes[1].uuid.clear();
  const bool owned = request.uuid == "node-a" && request.parent_uuid == "node-b";
  ImGui::DestroyContext();
  return owned ? 0 : 4;
} catch (...) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  return 5;
}
