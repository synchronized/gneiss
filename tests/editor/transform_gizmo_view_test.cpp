// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_view.hpp"

#include <gneiss/application.hpp>

#include <imgui.h>

#include <cmath>

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  application app;
  gneiss_world world_handle = GNEISS_NULL_WORLD;
  editor_session session;
  editor_command_history history;
  transform_gizmo_drag drag;
  scene_node_id first;
  scene_node_id second;
  const gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  if (application::create(desc, app).failed() || app.get_world(world_handle).failed() ||
      session.create_empty(app.get(), world_handle).failed() ||
      session.create_node("First", {}, first).failed() ||
      session.create_node("Second", {}, second).failed() || session.select(first).failed()) {
    return 1;
  }
  bool wait_release = false;
  result error = result::success;
  transform camera = GNEISS_TRANSFORM_IDENTITY;
  camera.translation[2] = 5.0F;
  const transform_gizmo_context context{
      .session = session,
      .history = history,
      .drag = drag,
      .world = world_ref{world_handle},
      .camera = camera,
      .operation = transform_gizmo_operation::translate,
      .wait_release = wait_release,
      .error = error,
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
  const auto draw = [&](ImVec2 size) {
    ImGui::NewFrame();
    begin_transform_gizmo_frame();
    ImGui::SetNextWindowSize({900.0F, 700.0F});
    ImGui::Begin("Scene");
    const auto active = draw_transform_gizmo(context, {0.0F, 0.0F}, size);
    ImGui::End();
    ImGui::Render();
    return active;
  };
  (void)draw({900.0F, 700.0F});
  if (error.failed() || drag.is_active() || history.can_undo()) {
    ImGui::DestroyContext();
    return 2;
  }
  transform moved = GNEISS_TRANSFORM_IDENTITY;
  moved.translation[0] = 2.0F;
  gizmo_matrix matrix{};
  if (transform_to_gizmo_matrix(moved, matrix).failed() || drag.begin(session).failed() ||
      drag.preview(session, world_handle, matrix).failed() || session.select(second).failed()) {
    ImGui::DestroyContext();
    return 3;
  }
  // 选择切换会提交原目标的拖动，而不会把预览写入新选择。
  if (!draw({900.0F, 700.0F}) || error.failed() || drag.is_active() || history.size() != 1U ||
      wait_release || history.undo().failed()) {
    ImGui::DestroyContext();
    return 4;
  }
  transform actual = GNEISS_TRANSFORM_IDENTITY;
  if (context.world.get_world_transform(first, actual).failed() ||
      std::abs(actual.translation[0]) > 0.0001F || session.select(first).failed() ||
      drag.begin(session).failed() || drag.preview(session, world_handle, matrix).failed()) {
    ImGui::DestroyContext();
    return 5;
  }
  // 隐藏/零尺寸视口同样收尾，不能留下活跃预览。
  if (draw({0.0F, 0.0F}) || error.failed() || drag.is_active() || !history.can_undo()) {
    ImGui::DestroyContext();
    return 6;
  }
  reset_transform_gizmo();
  ImGui::DestroyContext();
  history.clear();
  session.close();
  return 0;
} catch (...) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  return 7;
}
