// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_manager.h"

#include "editor_theme.h"
#include "imgui_adapter.h"
#include "native_dialog.h"
#include "project_workspace.h"

#include <gneiss/application.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
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
  std::array<char, 2048> create_path{};
  std::array<char, 256> create_name{};
  std::filesystem::path state_file;
  std::vector<editor_project> recent_projects;
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

void set_create_path(project_manager_state& state, const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  const auto length = std::min(text.size(), state.create_path.size() - 1U);
  std::memcpy(state.create_path.data(), text.data(), length);
  state.create_path[length] = '\0';
}

gneiss_result select_project(project_manager_state& state, gneiss_application application,
                             editor_project project) {
  (void)remember_recent_project(state.state_file, project);
  *state.output = std::move(project);
  state.selected = true;
  return gneiss_application_request_exit(application);
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
    constexpr ImVec2 card_size{620.0F, 315.0F};
    ImGui::SetCursorPos(ImVec2(50.0F, 48.0F));
    ImGui::BeginChild("ProjectCard", card_size, ImGuiChildFlags_Borders);
    ImGui::TextColored(theme_accent_color(), "GNEISS");
    ImGui::SameLine();
    ImGui::TextUnformatted("Project Manager");
    ImGui::Spacing();
    if (ImGui::BeginTabBar("ProjectActions")) {
      if (ImGui::BeginTabItem("Recent")) {
        if (state.recent_projects.empty()) {
          ImGui::TextDisabled("No recent projects");
        }
        for (std::size_t index = 0; index < state.recent_projects.size(); ++index) {
          const auto& project = state.recent_projects[index];
          ImGui::PushID(static_cast<int>(index));
          if (ImGui::Selectable(project.name.c_str())) {
            editor_project pending;
            state.operation = load_editor_project(project.project_root, pending);
            if (state.operation == result::success) {
              operation = select_project(state, application, std::move(pending));
            }
          }
          ImGui::SameLine(190.0F);
          ImGui::TextDisabled("%s", project.project_root.string().c_str());
          ImGui::PopID();
        }
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Open")) {
        ImGui::TextDisabled("Choose a directory containing gneiss.project.json");
        ImGui::SetNextItemWidth(454.0F);
        ImGui::InputTextWithHint("##ProjectPath", "Project root", state.project_path.data(),
                                 state.project_path.size());
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(118.0F, 0.0F))) {
          std::filesystem::path selected;
          state.operation = select_project_directory(selected);
          if (state.operation == result::success) {
            set_path(state, selected);
          }
        }
        ImGui::BeginDisabled(state.project_path[0] == '\0');
        const auto open_requested = ImGui::Button("Open Project", ImVec2(140.0F, 0.0F));
        ImGui::EndDisabled();
        if (open_requested) {
          editor_project pending;
          state.operation = load_editor_project(utf8_path(state.project_path.data()), pending);
          if (state.operation == result::success) {
            operation = select_project(state, application, std::move(pending));
          }
        }
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Create")) {
        ImGui::SetNextItemWidth(572.0F);
        ImGui::InputTextWithHint("##ProjectName", "Project name", state.create_name.data(),
                                 state.create_name.size());
        ImGui::SetNextItemWidth(454.0F);
        ImGui::InputTextWithHint("##CreatePath", "New project directory", state.create_path.data(),
                                 state.create_path.size());
        ImGui::SameLine();
        if (ImGui::Button("Parent...", ImVec2(118.0F, 0.0F))) {
          std::filesystem::path selected;
          state.operation = select_project_directory(selected);
          if (state.operation == result::success) {
            set_create_path(state, selected / "NewProject");
          }
        }
        const auto can_create = state.create_name[0] != '\0' && state.create_path[0] != '\0';
        ImGui::BeginDisabled(!can_create);
        const auto create_requested = ImGui::Button("Create Project", ImVec2(140.0F, 0.0F));
        ImGui::EndDisabled();
        if (create_requested) {
          editor_project pending;
          state.operation = create_editor_project(utf8_path(state.create_path.data()),
                                                  state.create_name.data(), pending);
          if (state.operation == result::success) {
            operation = select_project(state, application, std::move(pending));
          }
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    if (operation != GNEISS_SUCCESS) {
      ImGui::EndChild();
      ImGui::End();
      return operation;
    }
    if (state.operation != result::success && state.operation != result::not_ready) {
      const auto message = result_message(state.operation);
      ImGui::TextColored(theme_error_color(), "%.*s", static_cast<int>(message.size()),
                         message.data());
    }
    ImGui::EndChild();
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
    state.state_file = default_editor_state_path();
    const auto recent_result = load_recent_projects(state.state_file, state.recent_projects);
    if (recent_result != result::success && recent_result != result::invalid_argument) {
      state.operation = recent_result;
    }
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
      const auto message = result_message(operation);
      std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=Project Manager Application 创建，结果=%d，消息=%.*s\n",
                   to_native(operation), static_cast<int>(message.size()), message.data());
      return operation;
    }
    operation = from_native(state.ui.initialize(application.get()));
    if (operation != result::success) {
      const auto message = result_message(operation);
      std::fprintf(stderr, "Gneiss Editor 启动失败：阶段=Project Manager UI 初始化，结果=%d，消息=%.*s\n",
                   to_native(operation), static_cast<int>(message.size()), message.data());
    }
    if (operation == result::success) {
      operation = application.run(smoke ? 3U : 0U);
      if (operation != result::success) {
        const auto message = result_message(operation);
        std::fprintf(stderr, "Gneiss Editor 运行失败：阶段=Project Manager 事件循环，结果=%d，消息=%.*s\n",
                     to_native(operation), static_cast<int>(message.size()), message.data());
      }
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
