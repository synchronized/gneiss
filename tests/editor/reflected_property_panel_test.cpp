// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ui/panels/reflected_property_editor.hpp"
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/scene/scene_description.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <array>

int main() try {
  using namespace gneiss::editor;
  if (gneiss::initialize_meta().failed())
    return 6;
  gneiss::camera_description camera{{1.0F, 0.1F, 100.0F}};
  auto camera_view = gneiss::meta::view(std::as_const(camera));
  gneiss::math::transform transform;
  auto transform_view = gneiss::meta::view(std::as_const(transform));
  gneiss::mesh_component_description mesh{"asset://mesh.json", "asset://material.json"};
  auto mesh_view = gneiss::meta::view(std::as_const(mesh));
  unsigned writes = 0;
  std::uint64_t serial = 0;
  gneiss::error error = gneiss::error::success;
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
    if (draw_reflected_properties(transform_view, "Transform", serial, error))
      ++writes;
    const std::array readonly{gneiss::meta::property_id{4}};
    if (draw_reflected_properties(camera_view, "Camera", serial, error, readonly))
      ++writes;
    if (draw_reflected_properties(mesh_view, "Mesh Renderer", serial, error))
      ++writes;
    ImGui::End();
    ImGui::Render();
  };
  draw();
  const bool valid = error.ok() && ImGui::GetDrawData()->TotalVtxCount > 0;
  // 单独绘制目标属性，避免把组件末尾的只读身份字段误当成恢复按钮。
  const auto restore = [&](auto current, auto source, gneiss::meta::property_id property,
                           gneiss::value expected) {
    auto current_view = gneiss::meta::view(std::as_const(current));
    auto source_view = gneiss::meta::view(std::as_const(source));
    auto current_property =
        current_view.template capability<gneiss::meta::cap::object>()->member(property);
    auto source_property =
        source_view.template capability<gneiss::meta::cap::object>()->member(property);
    if (!current_property || !source_property)
      return false;
    std::optional<property_edit> requested;
    ImVec2 button;
    const auto frame = [&] {
      ImGui::NewFrame();
      ImGui::SetNextWindowPos({0, 0});
      ImGui::SetNextWindowSize({1000, 700});
      ImGui::Begin("Restore test");
      auto edit = draw_reflected_properties(*current_property, "Property", serial, error, {},
                                            &*source_property);
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
    auto unchanged =
        current_view.template capability<gneiss::meta::cap::object>()->member(property);
    auto original = source_view.template capability<gneiss::meta::cap::object>()->member(property);
    if (!unchanged || !original)
      return false;
    auto actual = unchanged->read_value();
    return requested && requested->path.empty() && requested->data.payload == expected.payload &&
           actual && actual->payload != expected.payload;
  };
  namespace e = gneiss;
  auto changed_transform = transform;
  changed_transform.scale = {2, 2, 2};
  if (!restore(changed_transform, transform, e::meta::property_id{3}, e::value{transform.scale}))
    return 10;
  auto changed_camera = camera.settings;
  changed_camera.far_plane = 200;
  if (!restore(changed_camera, camera.settings, e::meta::property_id{3},
               e::value{camera.settings.far_plane}))
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
      e::meta::register_enum(registry, {.type_id = e::runtime_type_of<choice>(),
                                        .kind = e::value_kind::int64,
                                        .entries = entries})
          .failed() ||
      e::meta::freeze(registry).failed())
    return 15;
  std::vector<choice> choices{choice::second};
  auto choices_view = e::meta::view(registry, std::as_const(choices));
  if (draw_reflected_properties(choices_view, "Enum elements", serial, error) || !error.ok())
    return 16;
  e::value uv{e::math::vector2{0.25F, 0.75F}};
  auto uv_view = e::meta::view(std::as_const(std::get<e::math::vector2>(uv.payload)));
  if (draw_property_value("UV", uv, uv_view.type(), serial, error) || !error.ok() ||
      uv.kind() != e::value_kind::vec2)
    return 13;
  ImGui::End();
  ImGui::Render();
  serial = 0;
  // 非法四元数只报告错误，不调用写入或变更模型；不需要原生窗口。
  transform.rotation.w = 0.0F;
  draw();
  const bool invalid = error == gneiss::error::invalid_argument;
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
