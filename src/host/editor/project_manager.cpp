// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_manager.hpp"

#include "editor_theme.hpp"
#include "imgui_adapter.hpp"
#include "native_dialog.hpp"
#include "project_workspace.hpp"

#include "engine/core/result.hpp"
#include "runtime/application.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace gneiss::editor {
namespace {

struct project_manager_state final {
  runtime::application* application{};
  imgui_adapter ui;
  editor_project* output = nullptr;
  std::array<char, 2048> project_path{};
  std::array<char, 2048> create_path{};
  std::array<char, 256> create_name{};
  std::filesystem::path state_file;
  std::vector<editor_project> recent_projects;
  gneiss::engine::core::result operation = gneiss::engine::core::result::success;
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

gneiss::engine::core::result select_project(project_manager_state& state, editor_project project) {
  (void)remember_recent_project(state.state_file, project);
  *state.output = std::move(project);
  state.selected = true;
  state.application->request_exit();
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result update_project_manager(project_manager_state& state,
                                                    const gneiss::engine::core::frame_time& time) {
  try {
    auto operation = state.ui.begin_frame(*state.application, time);
    if (operation != gneiss::engine::core::result::success) {
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
            if (state.operation == gneiss::engine::core::result::success) {
              operation = select_project(state, std::move(pending));
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
          if (state.operation == gneiss::engine::core::result::success) {
            set_path(state, selected);
          }
        }
        ImGui::BeginDisabled(state.project_path[0] == '\0');
        const auto open_requested = ImGui::Button("Open Project", ImVec2(140.0F, 0.0F));
        ImGui::EndDisabled();
        if (open_requested) {
          editor_project pending;
          state.operation = load_editor_project(utf8_path(state.project_path.data()), pending);
          if (state.operation == gneiss::engine::core::result::success) {
            operation = select_project(state, std::move(pending));
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
          if (state.operation == gneiss::engine::core::result::success) {
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
          if (state.operation == gneiss::engine::core::result::success) {
            operation = select_project(state, std::move(pending));
          }
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    if (operation != gneiss::engine::core::result::success) {
      ImGui::EndChild();
      ImGui::End();
      return operation;
    }
    if (state.operation != gneiss::engine::core::result::success &&
        state.operation != gneiss::engine::core::result::not_ready) {
      const auto message = state.operation.message();
      ImGui::TextColored(theme_error_color(), "%.*s", static_cast<int>(message.size()),
                         message.data());
    }
    ImGui::EndChild();
    ImGui::End();
    return state.ui.submit(*state.application);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace

gneiss::engine::core::result run_project_manager(bool smoke, editor_project& output) noexcept {
  try {
    project_manager_state state;
    state.output = &output;
    state.state_file = default_editor_state_path();
    const auto recent_result = load_recent_projects(state.state_file, state.recent_projects);
    if (recent_result != gneiss::engine::core::result::success &&
        recent_result != gneiss::engine::core::result::invalid_argument) {
      state.operation = recent_result;
    }
    std::shared_ptr<gneiss::runtime::application> application;
    constexpr std::string_view title = "Gneiss Project Manager";
    gneiss::runtime::application_configuration desc;
    desc.callbacks.update = [&state](gneiss::engine::core::rid,
                                     const gneiss::engine::core::frame_time& time) {
      return update_project_manager(state, time);
    };
    desc.use_granit_window = true;
    desc.window.title = title;

    desc.window.width = 720U;
    desc.window.height = 420U;
    desc.window.resizable = false;
    desc.window.high_dpi = false;
    auto operation = gneiss::runtime::application::create(desc, application);
    if (operation != gneiss::engine::core::result::success) {
      const auto message = operation.message();
      std::fprintf(
          stderr,
          "Gneiss Editor 启动失败：阶段=Project Manager Application 创建，结果=%d，消息=%.*s\n",
          operation.code(), static_cast<int>(message.size()), message.data());
      return operation;
    }
    state.application = application.get();
    operation = state.ui.initialize(*application);
    if (operation != gneiss::engine::core::result::success) {
      const auto message = operation.message();
      std::fprintf(stderr,
                   "Gneiss Editor 启动失败：阶段=Project Manager UI 初始化，结果=%d，消息=%.*s\n",
                   operation.code(), static_cast<int>(message.size()), message.data());
    }
    if (operation == gneiss::engine::core::result::success) {
      operation = application->run(gneiss::engine::core::null_rid, smoke ? 3U : 0U);
      if (operation != gneiss::engine::core::result::success) {
        const auto message = operation.message();
        std::fprintf(stderr,
                     "Gneiss Editor 运行失败：阶段=Project Manager 事件循环，结果=%d，消息=%.*s\n",
                     operation.code(), static_cast<int>(message.size()), message.data());
      }
    }
    state.ui.shutdown(*application);
    application.reset();
    if (operation != gneiss::engine::core::result::success) {
      return operation;
    }
    return state.selected ? gneiss::engine::core::result::success
                          : gneiss::engine::core::result::not_ready;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::editor
