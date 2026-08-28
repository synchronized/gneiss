// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_session.h"
#include "imgui_adapter.h"

#include <gneiss/application.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace {

struct editor_state {
  gneiss::editor::imgui_adapter ui;
  gneiss::editor::editor_session session;
};

struct launch_options {
  bool smoke = false;
  std::string asset_root;
  std::string scene_uri;
};

bool parse_options(int argc, char** argv, launch_options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--smoke") {
      options.smoke = true;
    } else if ((argument == "--asset-root" || argument == "--scene") && index + 1 < argc) {
      auto& destination = argument == "--asset-root" ? options.asset_root : options.scene_uri;
      destination = argv[++index];
    } else {
      return false;
    }
  }
  return options.scene_uri.empty() || !options.asset_root.empty();
}

void draw_scene_node(gneiss::editor::editor_session& session,
                     const gneiss::editor::scene_node_record& node) {
  const auto& nodes = session.nodes();
  const auto has_children = std::ranges::any_of(
      nodes, [node_id = node.node](const auto& candidate) { return candidate.parent == node_id; });
  auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (!has_children) {
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  }
  if (session.selection() == node.node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  ImGui::PushID(node.uuid.c_str());
  const auto is_open = ImGui::TreeNodeEx(node.display_name.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    (void)session.select(node.node);
  }
  if (has_children && is_open) {
    for (const auto& child : nodes) {
      if (child.parent == node.node) {
        draw_scene_node(session, child);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

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
    const auto selection_result = state.session.validate_selection();
    if (selection_result != gneiss::result::success &&
        selection_result != gneiss::result::invalid_handle) {
      return gneiss::to_native(selection_result);
    }

    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(250.0F, 720.0F));
    ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (!state.session.is_open()) {
      ImGui::TextUnformatted("No scene is open");
    } else {
      for (const auto& node : state.session.nodes()) {
        if (!node.parent.is_valid()) {
          draw_scene_node(state.session, node);
        }
      }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(250.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(730.0F, 720.0F));
    ImGui::Begin("Scene View", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::TextUnformatted("Gneiss Editor 0.7.0 development preview");
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(980.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(300.0F, 720.0F));
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (const auto* selected = state.session.selected_node(); selected != nullptr) {
      ImGui::Text("Name: %s", selected->display_name.c_str());
      ImGui::Text("UUID: %s", selected->uuid.c_str());
      ImGui::Text("Entity: %llu", static_cast<unsigned long long>(selected->entity.get()));
    } else {
      ImGui::TextUnformatted("No node is selected");
    }
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
  launch_options options;
  if (!parse_options(argc, argv, options)) {
    return 64;
  }
  if (options.asset_root.size() > std::numeric_limits<std::uint32_t>::max()) {
    return 64;
  }
  gneiss::application application;
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
  if (!options.asset_root.empty()) {
    desc.asset_root = options.asset_root.c_str();
    desc.asset_root_length = static_cast<std::uint32_t>(options.asset_root.size());
  }

  if (gneiss::application::create(desc, application) != gneiss::result::success) {
    return 1;
  }
  if (state.ui.initialize(application.get()) != GNEISS_SUCCESS) {
    return 2;
  }
  gneiss_world world = GNEISS_NULL_WORLD;
  if (application.get_world(world) != gneiss::result::success ||
      (!options.scene_uri.empty() &&
       state.session.open(application.get(), world, options.scene_uri) !=
           gneiss::result::success)) {
    state.ui.shutdown(application.get());
    return 3;
  }
  const auto run_result = application.run(options.smoke ? 3U : 0U);
  state.ui.shutdown(application.get());
  state.session.close();
  return run_result == gneiss::result::success ? 0 : 4;
}

} // namespace

int main(int argc, char** argv) {
  try {
    return run_editor(argc, argv);
  } catch (...) {
    return 99;
  }
}
