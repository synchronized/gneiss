// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "panels/author_property_panel.hpp"

#include <imgui.h>

#include <array>

int main() try {
  using namespace gneiss::editor;
  inspector_component component;
  component.name = "Transform";
  inspector_property property;
  property.id = gneiss::field_id{1U};
  property.name = "Rotation";
  property.kind = gneiss::property_kind::quaternion;
  property.capabilities = gneiss::property_capabilities::writable;
  property.value.payload = gneiss::property_quaternion{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
  component.properties.push_back(property);
  std::array components{component};
  unsigned writes = 0U;
  std::uint64_t serial = 0U;
  gneiss::engine::result error = gneiss::engine::result::success;
  const author_property_actions actions{
      .context = &writes,
      .write =
          [](void* context, const inspector_component&, const inspector_property&,
             const gneiss::engine::property_value&, std::uint64_t) {
            ++*static_cast<unsigned*>(context);
            return gneiss::engine::result{gneiss::engine::result::success};
          },
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
    ImGui::SetNextWindowSize({800.0F, 500.0F});
    ImGui::Begin("Inspector");
    draw_author_properties(components, serial, error, actions);
    ImGui::End();
    ImGui::Render();
  };
  draw();
  const bool valid = error.ok() && ImGui::GetDrawData()->TotalVtxCount > 0;
  // 非法四元数只报告错误，不调用写入或变更模型；不需要原生窗口。
  std::get<gneiss::property_quaternion>(components[0].properties[0].value.payload).w = 0.0F;
  draw();
  const bool invalid = error == gneiss::engine::result::invalid_argument;
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
