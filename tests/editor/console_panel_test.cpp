// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "console_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <string_view>

namespace {
bool verify_visible_state() {
  using namespace gneiss::editor;
  console_model model{2U};
  console_panel_state state;
  const auto session = model.begin_session();
  std::vector<std::size_t> visible;
  state.set_paused(model, true);
  if (model.append_raw(session, "first").failed() ||
      state.visible_indices(model, visible).failed() || !visible.empty()) {
    return false;
  }
  state.set_paused(model, false);
  if (state.visible_indices(model, visible).failed() || visible.size() != 1U) {
    return false;
  }
  state.set_paused(model, true);
  if (model.append_raw(session, "second").failed() ||
      state.visible_indices(model, visible).failed() || visible.size() != 1U || visible[0] != 0U) {
    return false;
  }
  // 暂停时仍允许日志淘汰；截止记录已被淘汰后不能把新日志误当成旧记录。
  if (model.append_raw(session, "third").failed() || model.dropped_count() != 1U ||
      state.visible_indices(model, visible).failed() || !visible.empty()) {
    return false;
  }
  state.set_paused(model, false);
  model.clear();
  const auto next_session = model.begin_session();
  if (model.append_raw(session, "old").failed() ||
      model
          .append_event(next_session,
                        {
                            .severity = GNEISS_LOG_ERROR,
                            .source = "game",
                            .category = "asset",
                            .message = "failed",
                        })
          .failed()) {
    return false;
  }
  state.filter.current_session_only = true;
  std::ranges::copy(std::string_view{"fail"}, state.search.begin());
  std::ranges::copy(std::string_view{"game"}, state.source.begin());
  std::ranges::copy(std::string_view{"asset"}, state.category.begin());
  if (state.visible_indices(model, visible).failed() || visible.size() != 1U || visible[0] != 1U) {
    return false;
  }
  state.filter.severity_mask = 0U;
  return state.visible_indices(model, visible).ok() && visible.empty();
}
} // namespace

int main() try {
  if (!verify_visible_state()) {
    return 1;
  }
  using namespace gneiss::editor;
  console_model model;
  const auto session = model.begin_session();
  if (model.append_raw(session, "raw output", true).failed()) {
    return 2;
  }
  console_panel_state state;
  bool is_open = true;
  unsigned clears = 0U;
  const console_panel_actions actions{
      .context = &clears,
      .clear = [](void* context) { ++*static_cast<unsigned*>(context); },
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
  ImGui::NewFrame();
  ImGui::SetNextWindowSize({900.0F, 500.0F});
  draw_console_panel(model, state, is_open,
                     {
                         .running = true,
                         .attempted = true,
                         .operation = gneiss::result::io,
                         .log_path = "borrowed/runtime.log",
                     },
                     actions);
  ImGui::Render();
  const bool drew = ImGui::GetDrawData()->TotalVtxCount > 0;
  ImGui::DestroyContext();
  return drew && is_open && clears == 0U && model.entries().size() == 1U ? 0 : 3;
} catch (...) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  return 4;
}
