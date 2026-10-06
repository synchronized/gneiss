// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_browser_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

int main() try {
  using namespace gneiss::editor;
  asset_browser_model model;
  const std::filesystem::path root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  if (model.refresh(root.parent_path(), root) != asset_browser_result::success ||
      !model.select("asset:models/triangle.mesh.json")) {
    return 1;
  }
  const auto mesh = std::ranges::find(model.entries(), model.selection(), &asset_browser_entry::id);
  const auto* material = find_material_for_mesh(model.entries(), *mesh);
  if (!is_mesh_asset(*mesh) || material == nullptr || !is_material_asset(*material) ||
      material->asset_uri != "asset://materials/triangle.material.json") {
    return 2;
  }
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1024.0F, 768.0F};
  io.DeltaTime = 1.0F / 60.0F;
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  bool open = true;
  ImGuiTextFilter filter;
  asset_browser_view view;
  view.worker_stage = "Idle";
  const auto draw = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0F, 0.0F});
    ImGui::SetNextWindowSize({900.0F, 700.0F});
    auto request = draw_asset_browser_panel(model, filter, open, view);
    ImGui::Render();
    return request;
  };
  if (draw().action != asset_browser_action::none || ImGui::GetDrawData()->TotalVtxCount == 0) {
    ImGui::DestroyContext();
    return 3;
  }
  // 默认状态的第一个控件为补扫按钮；通过真实 ImGui 输入产生请求，无原生窗口。
  const auto& style = ImGui::GetStyle();
  io.AddMousePosEvent(style.WindowPadding.x + 4.0F,
                      ImGui::GetFrameHeight() + style.WindowPadding.y + 4.0F);
  (void)draw();
  io.AddMouseButtonEvent(0, true);
  (void)draw();
  io.AddMouseButtonEvent(0, false);
  const auto request = draw();
  const auto selected = model.selection();
  if (request.action != asset_browser_action::scan_author_assets || request.asset_id != selected ||
      !model.select("asset:materials/triangle.material.json") || request.asset_id != selected) {
    ImGui::DestroyContext();
    return 4;
  }
  view.watches[0].waiting = true;
  view.watches[1].operation = gneiss::result::not_found;
  view.watches[1].root = "missing/assets";
  view.watches[1].dropped = 2U;
  view.source_scan_result = gneiss::result::io;
  view.import_attempted = true;
  view.import_diagnostic = "Import failed";
  view.scene.visible = true;
  view.scene.phase = "Load failed";
  view.scene.uri = "asset://scenes/missing.scene.json";
  view.scene.can_retry = true;
  view.scene.budget = asset_budget_view{};
  view.reload.visible = true;
  view.reload.tone = asset_status_tone::error;
  view.reload.publish_result = gneiss::result::io;
  view.reload.restart_required = true;
  view.author_change_visible = true;
  view.author_change_tone = asset_status_tone::error;
  view.author_uri = "asset://scenes/triangle.scene.json";
  view.author_message = "Unsaved document conflict";
  if (draw().action != asset_browser_action::none) {
    ImGui::DestroyContext();
    return 5;
  }
  ImGui::DestroyContext();
  return open ? 0 : 6;
} catch (...) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  return 7;
}
