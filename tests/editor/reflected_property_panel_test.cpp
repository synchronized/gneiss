// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/meta_registration.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include "panels/reflected_property_editor.hpp"

#include <imgui.h>

#include <array>

int main() try {
  using namespace gneiss::editor;
  if (gneiss::engine::initialize_meta().failed())
    return 6;
  gneiss::engine::camera_description camera{.settings = {1.0F, 0.1F, 100.0F}};
  auto camera_view = gneiss::engine::meta::instance::from(camera);
  if (!camera_view)
    return 7;
  gneiss::engine::math::transform transform;
  auto transform_view = gneiss::engine::meta::instance::from(transform);
  if (!transform_view)
    return 8;
  gneiss::engine::mesh_renderer_description mesh{"asset://mesh.json", "asset://material.json"};
  auto mesh_view = gneiss::engine::meta::instance::from(mesh);
  if (!mesh_view)
    return 9;
  unsigned writes = 0;
  std::uint64_t serial = 0;
  gneiss::engine::error error = gneiss::engine::error::success;
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
    ImGui::SetNextWindowSize({800.0F, 500.0F});
    ImGui::Begin("Inspector");
    if (draw_reflected_properties(*transform_view, "Transform", serial, error))
      ++writes;
    const std::array readonly{gneiss::engine::meta::property_id{4}};
    if (draw_reflected_properties(*camera_view, "Camera", serial, error, readonly))
      ++writes;
    if (draw_reflected_properties(*mesh_view, "Mesh Renderer", serial, error))
      ++writes;
    ImGui::End();
    ImGui::Render();
  };
  draw();
  const bool valid = error.ok() && ImGui::GetDrawData()->TotalVtxCount > 0;
  // 非法四元数只报告错误，不调用写入或变更模型；不需要原生窗口。
  transform.rotation.w = 0.0F;
  draw();
  const bool invalid = error == gneiss::engine::error::invalid_argument;
  ImGui::DestroyContext();
  if (!valid) {
    return 3;
  }
  if (!invalid) {
    return 4;
  }
  return writes == 0U && serial == 0U ? 0 : 5;
} catch (...) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  return 2;
}
