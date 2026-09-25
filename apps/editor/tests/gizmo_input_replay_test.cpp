// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_math.h"

#include <imgui.h>

#include <ImGuizmo.h>

#include <cmath>
#include <cstdio>

namespace {

bool replay(float scale, ImGuizmo::OPERATION operation) {
  ImGui::CreateContext();
  ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(800.0F * scale, 600.0F * scale);
  io.DeltaTime = 1.0F / 60.0F;
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  gneiss::transform camera = GNEISS_TRANSFORM_IDENTITY;
  camera.translation[1] = 2.0F;
  camera.translation[2] = 6.0F;
  const auto pitch = -0.5F * std::atan2(2.0F, 6.0F);
  camera.rotation[0] = std::sin(pitch);
  camera.rotation[3] = std::cos(pitch);
  auto view = gneiss::editor::build_gizmo_view_matrix(camera);
  auto projection = gneiss::editor::build_gizmo_projection_matrix(800.0F / 600.0F);
  gneiss::editor::gizmo_matrix model{};
  const gneiss::transform identity = GNEISS_TRANSFORM_IDENTITY;
  if (gneiss::editor::transform_to_gizmo_matrix(identity, model) != gneiss::result::success) {
    return false;
  }
  const auto initial = model;
  const auto frame = [&](float x, float y, bool pressed) {
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, pressed);
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    const auto changed = ImGuizmo::Manipulate(view.data(), projection.data(), operation,
                                              ImGuizmo::WORLD, model.data());
    ImGui::End();
    ImGui::Render();
    return changed;
  };
  (void)frame(0, 0, false);
  (void)frame(0, 0, false);
  float hit_x = 0.0F;
  float hit_y = 0.0F;
  bool found = false;
  // 搜索控件实际命中区域，避免把第三方轴的像素长度写死在测试中。
  for (int offset_y = -110; offset_y <= 110 && !found; offset_y += 3) {
    for (int offset_x = -110; offset_x <= 110 && !found; offset_x += 3) {
      hit_x = (400.0F + static_cast<float>(offset_x)) * scale;
      hit_y = (300.0F + static_cast<float>(offset_y)) * scale;
      (void)frame(hit_x, hit_y, false);
      found = ImGuizmo::IsOver(operation);
    }
  }
  (void)frame(hit_x, hit_y, true);
  const auto captured = ImGuizmo::IsUsing();
  bool changed = false;
  for (int index = 1; index <= 4; ++index) {
    changed |= frame(hit_x + (static_cast<float>(index) * 8.0F * scale),
                     hit_y + (static_cast<float>(index) * 4.0F * scale), true);
  }
  (void)frame(hit_x + (32.0F * scale), hit_y + (16.0F * scale), false);
  const auto released = !ImGuizmo::IsUsing();
  gneiss::transform result = GNEISS_TRANSFORM_IDENTITY;
  const auto valid =
      gneiss::editor::gizmo_matrix_to_transform(model, result) == gneiss::result::success;
  const auto success = found && captured && changed && released && valid && initial != model;
  if (!valid) {
    for (const auto value : model) {
      std::printf(" %.4f", static_cast<double>(value));
    }
    std::puts("");
  }
  std::printf("scale=%.2f operation=%d hit=%d captured=%d changed=%d released=%d valid=%d\n",
              static_cast<double>(scale), static_cast<int>(operation), static_cast<int>(found),
              static_cast<int>(captured), static_cast<int>(changed), static_cast<int>(released),
              static_cast<int>(valid));
  ImGui::DestroyContext();
  return success;
}

} // namespace

int main() {
  for (const float scale : {1.0F, 1.25F, 1.5F, 2.0F}) {
    for (const auto operation : {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE}) {
      if (!replay(scale, operation)) {
        return 1;
      }
    }
  }
  return 0;
}
