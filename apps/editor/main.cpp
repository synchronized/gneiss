// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "imgui_adapter.h"

#include <gneiss/application.hpp>

#include <imgui.h>

#include <cstdint>
#include <new>
#include <string_view>

namespace {

struct editor_state {
  gneiss::editor::imgui_adapter ui;
};

gneiss_result update_editor(gneiss_application application, const gneiss_frame_time* time,
                            void* user_data) {
  try {
    if (time == nullptr || user_data == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    auto& state = *static_cast<editor_state*>(user_data);
    auto result = state.ui.begin_frame(application, *time);
    if (result != GNEISS_SUCCESS) {
      return result;
    }

    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(250.0F, 720.0F));
    ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::TextUnformatted("No scene is open");
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(250.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(730.0F, 720.0F));
    ImGui::Begin("Scene View", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::TextUnformatted("Gneiss Editor 0.7.0 development preview");
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(980.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(300.0F, 720.0F));
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::TextUnformatted("No node is selected");
    ImGui::End();
    return state.ui.submit(application);
  } catch (const std::bad_alloc&) {
    // C++ 异常不得越过 C ABI 回调边界。
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

int run_editor(int argc, char** argv) {
  const bool smoke = argc == 2 && std::string_view{argv[1]} == "--smoke";
  editor_state state;
  constexpr std::string_view title = "Gneiss Editor";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_editor;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.window_width = 1280;
  desc.window_height = 720;
  desc.window_flags = GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;

  gneiss::application application;
  if (gneiss::application::create(desc, application) != gneiss::result::success) {
    return 1;
  }
  if (state.ui.initialize(application.get()) != GNEISS_SUCCESS) {
    return 2;
  }
  const auto run_result = application.run(smoke ? 3U : 0U);
  state.ui.shutdown(application.get());
  return run_result == gneiss::result::success ? 0 : 3;
}

} // namespace

int main(int argc, char** argv) {
  try {
    return run_editor(argc, argv);
  } catch (...) {
    return 99;
  }
}
