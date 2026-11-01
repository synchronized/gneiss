// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "panels/reflected_property_editor.hpp"
#include "engine/core/meta/serialization.hpp"
#include "viewport/editor_rotation_math.hpp"
#include <cstring>
#include <imgui.h>
namespace gneiss::editor {
using namespace engine;
namespace {
int resize_text(ImGuiInputTextCallbackData* data) {
  auto& text = *static_cast<std::string*>(data->UserData);
  text.resize(static_cast<std::size_t>(data->BufTextLen));
  data->Buf = text.data();
  return 0;
}
void draw_value(const meta::instance& view, const std::string& name, property_path& path,
                std::optional<property_edit>& pending, std::uint64_t& serial, error& status,
                std::span<const meta::property_id> readonly = {},
                const meta::instance* source = nullptr) {
  const auto value_type = view.type();
  if (value_type.kind() == meta::type_kind::object ||
      value_type.kind() == meta::type_kind::sequence) {
    const bool expanded = ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (value_type.kind() == meta::type_kind::object && !path.empty() &&
        ImGui::BeginPopupContextItem()) {
      const auto prefix = "gneiss-object:" + value_type.guid().string() + "\n";
      if (ImGui::MenuItem("Copy Object")) {
        auto address = view.object_data();
        auto bytes = address ? meta::serialize_object(value_type.id(), *address)
                             : result<std::vector<std::byte>, meta::serialization_diagnostic>{
                                   meta::serialization_diagnostic{.error = address.error()}};
        if (bytes) {
          auto text =
              prefix + std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
          ImGui::SetClipboardText(text.c_str());
        } else
          status = bytes.error().error;
      }
      const char* clipboard = ImGui::GetClipboardText();
      const std::string_view text = clipboard ? clipboard : "";
      const bool paste = ImGui::MenuItem("Paste Object", nullptr, false, text.starts_with(prefix));
      const bool reset = ImGui::MenuItem("Reset Object to Defaults");
      if (paste || reset) {
        const auto content = reset ? std::string_view{"{}"} : text.substr(prefix.size());
        auto decoded = meta::deserialize_object(
            value_type.id(), std::as_bytes(std::span{content.data(), content.size()}));
        if (!decoded)
          status = decoded.error().error;
        else
          pending = property_edit{
              .path = path, .object_type = value_type.id(), .object = std::move(*decoded)};
      }
      ImGui::EndPopup();
    }
    if (!expanded)
      return;
    if (value_type.kind() == meta::type_kind::object) {
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
          const bool writable =
              !(path.size() == 1 && std::ranges::find(readonly, property.id) != readonly.end()) &&
              !meta::has_flags(property.flags, meta::property_flags::read_only) &&
              !meta::has_flags(property.flags, meta::property_flags::editor_read_only) &&
              meta::has_flags(property.capabilities, meta::property_capabilities::writable);
          ImGui::BeginDisabled(!writable);
          std::optional<meta::instance> original;
          if (source && writable) {
            auto member = source->property(property.id);
            if (member)
              original = *member;
          }
          draw_value(*child, std::string(property.name), path, pending, serial, status, readonly,
                     original ? &*original : nullptr);
          ImGui::EndDisabled();
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
            std::optional<meta::instance> original;
            if (source) {
              auto sequence_source = source->as_sequence();
              if (sequence_source) {
                auto value = sequence_source->at(i);
                if (value)
                  original = *value;
              }
            }
            draw_value(*child, "[" + std::to_string(i) + "]", path, pending, serial, status,
                       readonly, original ? &*original : nullptr);
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
  auto original = source ? source->read_value() : result<value>{error::not_found};
  const bool overridden = original && original->payload != data->payload;
  if (draw_property_value(name.c_str(), *data, view.type(), serial, status))
    pending = property_edit{.path = path, .data = std::move(*data)};
  if (original) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", overridden ? "Overridden" : "Inherited");
    if (overridden) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Restore Source")) {
        ++serial;
        pending = property_edit{.path = path, .data = std::move(*original)};
      }
    }
  }
}
}
bool draw_property_value(const char* name, value& data, const meta::type& type,
                         std::uint64_t& serial, error& status) {
  if (type.kind() != meta::type_kind::scalar || type.scalar_kind() != data.kind()) {
    status = error::invalid_argument;
    return false;
  }
  std::span<const meta::enum_entry> choices;
  if (type.is_enum()) {
    auto entries = type.enum_entries();
    if (!entries) {
      status = entries.error();
      return false;
    }
    choices = *entries;
  }
  bool changed = false;
  error operation = error::success;
  if (!choices.empty()) {
    const char* selected = "Unknown";
    for (const auto& choice : choices)
      if (choice.data.payload == data.payload)
        selected = choice.name.c_str();
    if (ImGui::BeginCombo(name, selected)) {
      for (const auto& choice : choices)
        if (ImGui::Selectable(choice.name.c_str(), choice.data.payload == data.payload)) {
          data = choice.data;
          changed = true;
          ++serial;
        }
      ImGui::EndCombo();
    }
  } else
    switch (type.scalar_kind()) {
    case value_kind::boolean:
      changed = ImGui::Checkbox(name, &std::get<bool>(data.payload));
      break;
    case value_kind::float32:
      changed = ImGui::DragFloat(name, &std::get<float>(data.payload), 0.01F);
      break;
    case value_kind::vec2:
      changed = ImGui::DragFloat2(name, &std::get<math::vector2>(data.payload).x, 0.01F);
      break;
    case value_kind::vec3:
      changed = ImGui::DragFloat3(name, &std::get<math::vector3>(data.payload).x, 0.01F);
      break;
    case value_kind::quaternion: {
      std::array<float, 3> euler{};
      operation = quaternion_to_euler_degrees(std::get<math::quaternion>(data.payload), euler);
      if (operation.ok()) {
        changed = ImGui::DragFloat3(name, euler.data(), 0.25F, 0, 0, "%.1f°");
        if (changed)
          operation = euler_degrees_to_quaternion(euler, std::get<math::quaternion>(data.payload));
      }
      break;
    }
    case value_kind::vec4:
      changed = ImGui::DragFloat4(name, &std::get<math::vector4>(data.payload).x, 0.01F);
      break;
    case value_kind::uint64:
      changed = ImGui::InputScalar(name, ImGuiDataType_U64, &std::get<std::uint64_t>(data.payload));
      break;
    case value_kind::int64:
      changed = ImGui::InputScalar(name, ImGuiDataType_S64, &std::get<std::int64_t>(data.payload));
      break;
    case value_kind::string: {
      auto& text = std::get<std::string>(data.payload);
      changed = ImGui::InputText(name, text.data(), text.capacity() + 1,
                                 ImGuiInputTextFlags_CallbackResize |
                                     ImGuiInputTextFlags_EnterReturnsTrue,
                                 resize_text, &text);
      if (changed)
        text.resize(std::strlen(text.c_str()));
      break;
    }
    default:
      ImGui::TextDisabled("%s: unsupported", name);
      break;
    }
  if (ImGui::IsItemActivated())
    ++serial;
  if (operation.failed() || changed)
    status = operation;
  return changed && operation.ok();
}

std::optional<property_edit> draw_reflected_properties(const meta::instance& view, const char* name,
                                                       std::uint64_t& serial, error& status,
                                                       std::span<const meta::property_id> readonly,
                                                       const meta::instance* source) {
  if (source && source->type().id() != view.type().id()) {
    status = error::invalid_argument;
    return {};
  }
  std::optional<property_edit> edit;
  property_path path;
  draw_value(view, name, path, edit, serial, status, readonly, source);
  return edit;
}
}
