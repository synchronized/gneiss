// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ui.h"

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

void create_test_context() {
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
}

void draw_workspace_frame() {
  auto& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280.0F, 720.0F);
  io.DeltaTime = 1.0F / 60.0F;
  ImGui::NewFrame();
  gneiss::editor::begin_editor_workspace();
  for (const auto* name :
       {"Scene Hierarchy", "Asset Browser", "Scene View", "Inspector", "Console"}) {
    ImGui::Begin(name);
    ImGui::End();
  }
  ImGui::Render();
}

} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "gneiss-editor-ui-layout-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root / "project-a", error);
  std::filesystem::create_directories(root / "project-b", error);
  if (error) {
    return 1;
  }
  const auto state_file = root / "config" / "editor.json";

  create_test_context();
  if (gneiss::editor::initialize_editor_layout(state_file, root / "project-a") !=
      gneiss::result::success) {
    return 2;
  }
  draw_workspace_frame();
  if (gneiss::editor::save_editor_layout() != gneiss::result::success) {
    return 3;
  }
  ImGui::DestroyContext();

  const auto layouts = state_file.parent_path() / "layouts";
  auto iterator = std::filesystem::directory_iterator(layouts, error);
  if (error || iterator == std::filesystem::directory_iterator()) {
    return 4;
  }
  const auto first_layout = iterator->path();
  ++iterator;
  if (iterator != std::filesystem::directory_iterator()) {
    return 5;
  }
  std::ifstream stream(first_layout, std::ios::binary);
  const std::string saved{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  if (!saved.starts_with("GNEISS_EDITOR_LAYOUT 1\n") ||
      saved.find("Scene Hierarchy") == std::string::npos) {
    return 6;
  }

  create_test_context();
  if (gneiss::editor::initialize_editor_layout(state_file, root / "project-a") !=
      gneiss::result::success) {
    return 7;
  }
  if (gneiss::editor::initialize_editor_layout(state_file, root / "project-b") !=
      gneiss::result::success) {
    return 8;
  }
  draw_workspace_frame();
  if (gneiss::editor::save_editor_layout() != gneiss::result::success) {
    return 9;
  }
  ImGui::DestroyContext();

  std::size_t layout_count = 0U;
  for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(layouts)) {
    ++layout_count;
  }
  if (layout_count != 2U) {
    return 10;
  }

  std::ofstream incompatible(first_layout, std::ios::binary | std::ios::trunc);
  incompatible << "GNEISS_EDITOR_LAYOUT 2\nunsupported layout";
  incompatible.close();
  create_test_context();
  const auto incompatible_result =
      gneiss::editor::initialize_editor_layout(state_file, root / "project-a");
  ImGui::DestroyContext();
  if (incompatible_result != gneiss::result::invalid_argument) {
    return 11;
  }

  const auto blocked_root = root / "blocked";
  std::ofstream blocked(blocked_root, std::ios::binary);
  blocked << "file blocks directory creation";
  blocked.close();
  create_test_context();
  if (gneiss::editor::initialize_editor_layout(blocked_root / "editor.json", root / "project-a") !=
      gneiss::result::success) {
    return 12;
  }
  draw_workspace_frame();
  const auto blocked_result = gneiss::editor::save_editor_layout();
  ImGui::DestroyContext();
  std::filesystem::remove_all(root, error);
  return blocked_result == gneiss::result::io ? 0 : 13;
}
