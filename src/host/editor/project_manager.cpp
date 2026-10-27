// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_manager.hpp"
#include <thread>

#include "editor_theme.hpp"
#include "imgui_adapter.hpp"
#include "native_dialog.hpp"
#include "project_workspace.hpp"

#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
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

namespace gneiss::host {
namespace {

struct project_job_result {
  editor_project project;
  engine::result status{engine::result::internal};
};

struct project_manager_state final {
  engine::task_scheduler scheduler{{.workers = 1U}};
  engine::task_scope scope{scheduler.make_scope()};
  std::shared_ptr<project_job_result> pending;
  bool closing{};
  runtime::application* application{};
  gneiss::editor::imgui_adapter ui;
  editor_project* output = nullptr;
  std::array<char, 2048> project_path{};
  std::array<char, 2048> create_path{};
  std::array<char, 256> create_name{};
  std::filesystem::path state_file;
  std::vector<editor_project> recent_projects;
  gneiss::engine::result operation = gneiss::engine::result::success;
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

gneiss::engine::result select_project(project_manager_state& state, editor_project project) {
  *state.output = std::move(project);
  state.selected = true;
  state.application->request_exit();
  return gneiss::engine::result::success;
}

// 桌面工程任务拥有输入和结果，不借用 UI 或输出对象；只在主线程采用完成结果。
engine::result begin_project_job(project_manager_state& state, std::filesystem::path root,
                                 std::string name = {}) {
  if (state.pending || state.closing)
    return engine::result::not_ready;
  auto pending = std::make_shared<project_job_result>();
  engine::task_handle task;
  const auto submitted = state.scheduler.submit(
      {.name = "工程创建或打开", .scope = state.scope},
      [pending, state_file = state.state_file, root = std::move(root),
       name = std::move(name)](const engine::task_context&) {
        pending->status = name.empty() ? load_editor_project(root, pending->project)
                                       : create_editor_project(root, name, pending->project);
        if (pending->status.ok()) {
          const auto saved = remember_recent_project(state_file, pending->project);
          if (saved.failed())
            std::fprintf(stderr, "最近工程配置保存失败：%d\n", saved.code());
        }
        return engine::task_outcome{};
      },
      task);
  if (submitted != engine::submit_result::success)
    return engine::result::internal;
  state.pending = std::move(pending);
  return engine::result::success;
}

gneiss::engine::result update_project_manager(project_manager_state& state,
                                              const gneiss::engine::frame_time& time) {
  try {
    if (state.pending) {
      std::vector<engine::task_completion> completed;
      state.scheduler.poll(state.scope, completed);
      if (!completed.empty()) {
        auto pending = std::exchange(state.pending, {});
        state.operation = completed.front().outcome.state == engine::task_state::succeeded
                              ? pending->status
                              : engine::result::internal;
        if (!state.closing && state.operation.ok())
          (void)select_project(state, std::move(pending->project));
      }
    }
    if (state.closing && !state.pending)
      state.application->request_exit();
    auto operation = state.ui.begin_frame(*state.application, time);
    if (operation != gneiss::engine::result::success) {
      return operation;
    }

    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(720.0F, 420.0F));
    ImGui::Begin("Gneiss Project Manager", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    constexpr ImVec2 card_size{620.0F, 315.0F};
    ImGui::SetCursorPos(ImVec2(50.0F, 48.0F));
    ImGui::BeginChild("ProjectCard", card_size, ImGuiChildFlags_Borders);
    ImGui::TextColored(gneiss::editor::theme_accent_color(), "GNEISS");
    ImGui::SameLine();
    ImGui::TextUnformatted("Project Manager");
    ImGui::Spacing();
    ImGui::BeginDisabled(state.pending != nullptr || state.closing);
    if (ImGui::BeginTabBar("ProjectActions")) {
      if (ImGui::BeginTabItem("Recent")) {
        if (state.recent_projects.empty()) {
          ImGui::TextDisabled("No recent projects");
        }
        for (std::size_t index = 0; index < state.recent_projects.size(); ++index) {
          const auto& project = state.recent_projects[index];
          ImGui::PushID(static_cast<int>(index));
          if (ImGui::Selectable(project.name.c_str())) {
            state.operation = begin_project_job(state, project.project_root);
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
          if (state.operation == gneiss::engine::result::success) {
            set_path(state, selected);
          }
        }
        ImGui::BeginDisabled(state.project_path[0] == '\0');
        const auto open_requested = ImGui::Button("Open Project", ImVec2(140.0F, 0.0F));
        ImGui::EndDisabled();
        if (open_requested) {
          state.operation = begin_project_job(state, utf8_path(state.project_path.data()));
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
          if (state.operation == gneiss::engine::result::success) {
            set_create_path(state, selected / "NewProject");
          }
        }
        const auto can_create = state.create_name[0] != '\0' && state.create_path[0] != '\0';
        ImGui::BeginDisabled(!can_create);
        const auto create_requested = ImGui::Button("Create Project", ImVec2(140.0F, 0.0F));
        ImGui::EndDisabled();
        if (create_requested) {
          state.operation = begin_project_job(state, utf8_path(state.create_path.data()),
                                              state.create_name.data());
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::EndDisabled();
    if (state.pending)
      ImGui::TextUnformatted(state.closing ? "正在等待工程任务收尾……" : "正在处理工程……");
    if (operation != gneiss::engine::result::success) {
      ImGui::EndChild();
      ImGui::End();
      return operation;
    }
    if (state.operation != gneiss::engine::result::success &&
        state.operation != gneiss::engine::result::not_ready) {
      const auto message = state.operation.message();
      ImGui::TextColored(gneiss::editor::theme_error_color(), "%.*s",
                         static_cast<int>(message.size()), message.data());
    }
    ImGui::EndChild();
    ImGui::End();
    return state.ui.submit(*state.application);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

} // namespace

gneiss::engine::result run_project_manager(bool smoke, editor_project& output) noexcept {
  try {
    project_manager_state state;
    state.output = &output;
    state.state_file = default_editor_state_path();
    const auto recent_result = load_recent_projects(state.state_file, state.recent_projects);
    if (recent_result != gneiss::engine::result::success &&
        recent_result != gneiss::engine::result::invalid_argument) {
      state.operation = recent_result;
    }
    std::shared_ptr<gneiss::runtime::application> application;
    constexpr std::string_view title = "Gneiss Project Manager";
    gneiss::runtime::application_configuration desc;
    desc.callbacks.update = [&state](const gneiss::engine::frame_time& time) {
      return update_project_manager(state, time);
    };
    desc.callbacks.close_requested = [&state] {
      state.closing = true;
      return !state.pending;
    };
    desc.use_granit_window = true;
    desc.window.title = title;

    desc.window.width = 720U;
    desc.window.height = 420U;
    desc.window.resizable = false;
    desc.window.high_dpi = false;
    auto operation = gneiss::runtime::application::create(desc, application);
    if (operation != gneiss::engine::result::success) {
      const auto message = operation.message();
      std::fprintf(
          stderr,
          "Gneiss Editor 启动失败：阶段=Project Manager Application 创建，结果=%d，消息=%.*s\n",
          operation.code(), static_cast<int>(message.size()), message.data());
      return operation;
    }
    state.application = application.get();
    operation = state.ui.initialize(*application);
    if (operation != gneiss::engine::result::success) {
      const auto message = operation.message();
      std::fprintf(stderr,
                   "Gneiss Editor 启动失败：阶段=Project Manager UI 初始化，结果=%d，消息=%.*s\n",
                   operation.code(), static_cast<int>(message.size()), message.data());
    }
    if (operation == gneiss::engine::result::success) {
      operation = application->run(smoke ? 3U : 0U);
      if (operation != gneiss::engine::result::success) {
        const auto message = operation.message();
        std::fprintf(stderr,
                     "Gneiss Editor 运行失败：阶段=Project Manager 事件循环，结果=%d，消息=%.*s\n",
                     operation.code(), static_cast<int>(message.size()), message.data());
      }
    }
    // 异常离开事件循环时也显式等待任务；任务不访问已经销毁的 UI。
    (void)state.scheduler.close_scope(state.scope);
    state.pending.reset();
    state.ui.shutdown(*application);
    // 正常退出显式收尾文件回执与服务，不依赖 Application 析构等待。
    auto closing = application->request_shutdown();
    if (closing.ok()) {
      do {
        closing = application->advance_shutdown();
        if (closing == gneiss::engine::result::not_ready)
          std::this_thread::yield();
      } while (closing == gneiss::engine::result::not_ready);
    }
    if (operation.ok())
      operation = closing;
    application.reset();
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    return state.selected ? gneiss::engine::result::success : gneiss::engine::result::not_ready;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

} // namespace gneiss::host
