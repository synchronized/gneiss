// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_manager.h"

#include "imgui_adapter.h"
#include "native_dialog.h"

#include <gneiss/application.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace gneiss::editor {
namespace {

struct project_manager_state final {
  imgui_adapter ui;
  editor_project* output = nullptr;
  std::array<char, 2048> project_path{};
  result operation = result::success;
  bool selected = false;
};

[[nodiscard]] std::filesystem::path utf8_path(const char* value) {
  const std::string_view text(value);
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

void set_path(project_manager_state& state, const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  const auto length = std::min(text.size(), state.project_path.size() - 1U);
  std::memcpy(state.project_path.data(), text.data(), length);
  state.project_path[length] = '\0';
}

gneiss_result update_project_manager(gneiss_application application, const gneiss_frame_time* time,
                                     void* user_data) {
  try {
    if (time == nullptr || user_data == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    auto& state = *static_cast<project_manager_state*>(user_data);
    auto operation = state.ui.begin_frame(application, *time);
    if (operation != GNEISS_SUCCESS) {
      return operation;
    }

    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(720.0F, 420.0F));
    ImGui::Begin("Gneiss Project Manager", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Open a Gneiss project");
    ImGui::TextDisabled("Select a directory containing gneiss.project.json");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(520.0F);
    ImGui::InputText("##ProjectPath", state.project_path.data(), state.project_path.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
      std::filesystem::path selected;
      state.operation = select_project_directory(selected);
      if (state.operation == result::success) {
        set_path(state, selected);
      }
    }
    const auto has_path = state.project_path[0] != '\0';
    ImGui::BeginDisabled(!has_path);
    const auto open_requested = ImGui::Button("Open Project");
    ImGui::EndDisabled();
    if (open_requested) {
      editor_project pending;
      state.operation = load_editor_project(utf8_path(state.project_path.data()), pending);
      if (state.operation == result::success) {
        *state.output = std::move(pending);
        state.selected = true;
        operation = gneiss_application_request_exit(application);
        if (operation != GNEISS_SUCCESS) {
          ImGui::End();
          return operation;
        }
      }
    }
    if (state.operation != result::success && state.operation != result::not_ready) {
      const auto message = result_message(state.operation);
      ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.25F, 1.0F), "%.*s", static_cast<int>(message.size()),
                         message.data());
    }
    ImGui::End();
    return state.ui.submit(application);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace

result run_project_manager(bool smoke, editor_project& output) noexcept {
  try {
    project_manager_state state;
    state.output = &output;
    gneiss::application application;
    constexpr std::string_view title = "Gneiss Project Manager";
    gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
    desc.user_data = &state;
    desc.update = update_project_manager;
    desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
    desc.window_title = title.data();
    desc.window_title_length = static_cast<std::uint32_t>(title.size());
    desc.window_width = 720U;
    desc.window_height = 420U;
    desc.window_flags = GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
    auto operation = gneiss::application::create(desc, application);
    if (operation != result::success) {
      return operation;
    }
    operation = from_native(state.ui.initialize(application.get()));
    if (operation == result::success) {
      operation = application.run(smoke ? 3U : 0U);
    }
    state.ui.shutdown(application.get());
    application.reset();
    if (operation != result::success) {
      return operation;
    }
    return state.selected ? result::success : result::not_ready;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss::editor
