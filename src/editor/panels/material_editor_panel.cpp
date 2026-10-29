// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "panels/material_editor_panel.hpp"
#include <cstring>
#include <imgui.h>
#include <optional>
namespace gneiss::editor {
namespace {
using namespace engine;
struct edit {
  material_property_path path;
  value data;
};
int resize_text(ImGuiInputTextCallbackData* data) {
  auto& text = *static_cast<std::string*>(data->UserData);
  text.resize(static_cast<std::size_t>(data->BufTextLen));
  data->Buf = text.data();
  return 0;
}
void draw_value(const meta::instance& view, const std::string& name, material_property_path& path,
                std::optional<edit>& pending, std::uint64_t& serial, error& status) {
  if (view.kind() == meta::type_kind::object || view.kind() == meta::type_kind::sequence) {
    if (!ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
      return;
    if (view.kind() == meta::type_kind::object) {
      auto properties = view.properties();
      if (!properties)
        status = properties.error();
      else
        for (const auto& property : *properties) {
          auto child = view.property(property.id);
          if (!child) {
            status = child.error();
            continue;
          }
          path.emplace_back(property.id);
          ImGui::PushID(static_cast<int>(property.id.get()));
          draw_value(*child, std::string(property.name), path, pending, serial, status);
          ImGui::PopID();
          path.pop_back();
        }
    } else {
      auto sequence = view.as_sequence();
      if (!sequence)
        status = sequence.error();
      else {
        auto size = sequence->size();
        if (!size)
          status = size.error();
        else
          for (std::size_t i = 0; i < *size; ++i) {
            auto child = sequence->at(i);
            if (!child) {
              status = child.error();
              continue;
            }
            path.emplace_back(i);
            ImGui::PushID(static_cast<int>(i));
            draw_value(*child, "[" + std::to_string(i) + "]", path, pending, serial, status);
            ImGui::PopID();
            path.pop_back();
          }
      }
    }
    ImGui::TreePop();
    return;
  }
  auto data = view.read_value();
  if (!data) {
    status = data.error();
    return;
  }
  bool changed = false;
  switch (data->kind()) {
  case value_kind::boolean:
    changed = ImGui::Checkbox(name.c_str(), &std::get<bool>(data->payload));
    break;
  case value_kind::float32:
    changed = ImGui::DragFloat(name.c_str(), &std::get<float>(data->payload), 0.01F);
    break;
  case value_kind::vec3:
    changed = ImGui::DragFloat3(name.c_str(), &std::get<math::vector3>(data->payload).x, 0.01F);
    break;
  case value_kind::vec4:
    changed = ImGui::ColorEdit4(name.c_str(), &std::get<math::vector4>(data->payload).x);
    break;
  case value_kind::uint64:
    changed = ImGui::InputScalar(name.c_str(), ImGuiDataType_U64,
                                 &std::get<std::uint64_t>(data->payload));
    break;
  case value_kind::int64:
    changed =
        ImGui::InputScalar(name.c_str(), ImGuiDataType_S64, &std::get<std::int64_t>(data->payload));
    break;
  case value_kind::string: {
    auto& text = std::get<std::string>(data->payload);
    changed =
        ImGui::InputText(name.c_str(), text.data(), text.capacity() + 1,
                         ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
                         resize_text, &text);
    if (changed)
      text.resize(std::strlen(text.c_str()));
    break;
  }
  default:
    ImGui::TextDisabled("%s: unsupported", name.c_str());
    break;
  }
  if (ImGui::IsItemActivated())
    ++serial;
  if (changed)
    pending = edit{path, std::move(*data)};
}
}
void draw_material_editor(material_editor_model& model, engine::files::service& files,
                          bool& visible, std::uint64_t& serial, engine::error& status,
                          bool& focused) {
  focused = false;
  model.advance();
  if (!visible)
    return;
  bool open = true;
  if (ImGui::Begin("Material Editor", &open)) {
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !model.saving() && model.is_dirty())
        status = model.begin_save(files);
      if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (ImGui::GetIO().KeyShift && model.can_redo())
          status = model.redo();
        else if (!ImGui::GetIO().KeyShift && model.can_undo())
          status = model.undo();
      }
    }
    if (!model.is_open())
      ImGui::TextUnformatted("Select an authored material in Asset Browser.");
    else {
      ImGui::TextWrapped("%.*s%s", static_cast<int>(model.uri().size()), model.uri().data(),
                         model.is_dirty() ? " *" : "");
      ImGui::BeginDisabled(model.saving() || !model.is_dirty());
      if (ImGui::Button("Save Material"))
        status = model.begin_save(files);
      ImGui::SameLine();
      if (ImGui::Button("Discard Changes"))
        ImGui::OpenPopup("Discard material edits?");
      ImGui::EndDisabled();
      if (ImGui::BeginPopupModal("Discard material edits?", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Discard all unsaved changes to this material?");
        if (ImGui::Button("Discard")) {
          status = model.discard();
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::BeginDisabled(!model.can_undo());
      if (ImGui::Button("Undo Material"))
        status = model.undo();
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!model.can_redo());
      if (ImGui::Button("Redo Material"))
        status = model.redo();
      ImGui::EndDisabled();
      if (model.saving())
        ImGui::TextUnformatted("Saving...");
      else if (model.save_status().failed())
        ImGui::TextWrapped("Save failed: %s", model.save_status().message().data());
      ImGui::TextUnformatted("Texture URI: press Enter to apply. No live GPU preview.");
      std::optional<edit> pending;
      auto root = engine::meta::instance::from(model.description());
      if (!root)
        status = root.error();
      else {
        material_property_path path;
        draw_value(*root, "Material", path, pending, serial, status);
      }
      // 遍历结束后修改，避免更换快照使借用视图失效。
      if (pending)
        status = model.set_value(pending->path, pending->data, serial);
    }
    if (status == engine::error::invalid_state && model.is_dirty())
      ImGui::TextWrapped("Save or discard this material before closing or opening another file.");
    if (status.failed())
      ImGui::TextWrapped("%s", status.message().data());
  }
  ImGui::End();
  if (!open) {
    if (model.is_dirty() || model.saving())
      status = engine::error::invalid_state;
    else
      visible = false;
  }
}
}
