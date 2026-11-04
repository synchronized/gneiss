// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/meta_registration.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include "panels/reflected_property_editor.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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
  // 模拟点击实际面板的最后一个恢复按钮，验证独立值、路径及不直接写对象。
  const auto restore = [&](auto current, auto source, gneiss::engine::meta::property_id property,
                           gneiss::engine::value expected) {
    auto current_view = gneiss::engine::meta::instance::from(current);
    auto source_view = gneiss::engine::meta::instance::from(source);
    if (!current_view || !source_view)
      return false;
    std::optional<property_edit> requested;
    ImVec2 button;
    const auto frame = [&] {
      ImGui::NewFrame();
      ImGui::SetNextWindowPos({0, 0});
      ImGui::SetNextWindowSize({1000, 700});
      ImGui::Begin("Restore test");
      auto edit =
          draw_reflected_properties(*current_view, "Component", serial, error, {}, &*source_view);
      if (edit)
        requested = std::move(edit);
      button = GImGui->LastItemData.Rect.GetCenter();
      ImGui::End();
      ImGui::Render();
    };
    frame();
    frame();
    io.AddMousePosEvent(button.x, button.y);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    auto unchanged = current_view->property(property);
    auto original = source_view->property(property);
    if (!unchanged || !original)
      return false;
    auto actual = unchanged->read_value();
    return requested && requested->path == property_path{property} &&
           requested->data.payload == expected.payload && actual &&
           actual->payload != expected.payload;
  };
  namespace e = gneiss::engine;
  auto changed_transform = transform;
  changed_transform.scale = {2, 2, 2};
  if (!restore(changed_transform, transform, e::meta::property_id{3}, e::value{transform.scale}))
    return 10;
  auto changed_camera = camera.settings;
  changed_camera.far = 200;
  if (!restore(changed_camera, camera.settings, e::meta::property_id{3},
               e::value{camera.settings.far}))
    return 11;
  auto changed_mesh = mesh;
  changed_mesh.material_uri = "asset://changed.json";
  if (!restore(changed_mesh, mesh, e::meta::property_id{2}, e::value{mesh.material_uri}))
    return 12;
  ImGui::NewFrame();
  ImGui::Begin("Vector2");
  enum class choice { first, second };
  auto registry = e::meta::create_registry();
  const std::array entries{e::meta::enum_entry{"First", e::value{std::int64_t{0}}},
                           e::meta::enum_entry{"Second", e::value{std::int64_t{1}}}};
  if (!registry ||
      e::meta::register_enum(*registry, e::runtime_type_of<choice>(), e::value_kind::int64, entries)
          .failed() ||
      e::meta::freeze(*registry).failed())
    return 15;
  std::vector<choice> choices{choice::second};
  auto choices_view = e::meta::instance::from(*registry, choices);
  if (!choices_view || draw_reflected_properties(*choices_view, "Enum elements", serial, error) ||
      !error.ok())
    return 16;
  e::value uv{e::math::vector2{0.25F, 0.75F}};
  auto uv_view = e::meta::instance::from(std::get<e::math::vector2>(uv.payload));
  if (!uv_view)
    return 14;
  if (draw_property_value("UV", uv, uv_view->type(), serial, error) || !error.ok() ||
      uv.kind() != e::value_kind::vec2)
    return 13;
  ImGui::End();
  ImGui::Render();
  serial = 0;
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
