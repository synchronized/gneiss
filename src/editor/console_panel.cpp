// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "console_panel.hpp"
#include "editor_theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>

namespace gneiss::editor {
namespace {
std::string_view console_severity_name(std::uint32_t severity) noexcept {
  switch (severity) {
  case GNEISS_LOG_TRACE:
    return "TRACE";
  case GNEISS_LOG_DEBUG:
    return "DEBUG";
  case GNEISS_LOG_INFO:
    return "INFO";
  case GNEISS_LOG_WARNING:
    return "WARN";
  case GNEISS_LOG_ERROR:
    return "ERROR";
  case GNEISS_LOG_FATAL:
    return "FATAL";
  default:
    return "UNKNOWN";
  }
}

std::string format_console_entry(const gneiss::editor::console_entry& entry) {
  if (entry.kind == gneiss::editor::console_entry_kind::raw) {
    return std::string{"[RAW] "} + entry.raw_text +
           (entry.was_truncated ? " [line truncated]" : "");
  }
  std::string output{"["};
  output.append(console_severity_name(entry.event.severity));
  output.append("][");
  output.append(entry.event.source);
  output.append("][");
  output.append(entry.event.category);
  output.append("] ");
  output.append(entry.event.message);
  if (entry.event.operation != GNEISS_SUCCESS) {
    output.append(" (result=");
    output.append(std::to_string(entry.event.operation));
    output.push_back(')');
  }
  return output;
}

} // namespace

void console_panel_state::set_paused(const console_model& model, bool value) noexcept {
  paused = value;
  const auto& entries = model.entries();
  pause_entry_id = paused && !entries.empty() ? entries.back().id : 0U;
}

result console_panel_state::visible_indices(const console_model& model,
                                            std::vector<std::size_t>& output) {
  filter.search = search.data();
  filter.source = source.data();
  filter.category = category.data();
  const auto status = model.visible_indices(filter, output);
  if (paused) {
    std::erase_if(output,
                  [&](std::size_t index) { return model.entries()[index].id > pause_entry_id; });
  }
  return status;
}

namespace {
void draw_console_toolbar(const console_model& model, console_panel_state& state,
                          const console_runtime_view& runtime,
                          const console_panel_actions& actions) {
  if (runtime.building) {
    ImGui::TextColored(gneiss::editor::theme_warning_color(), "Building game module");
  } else if (runtime.running) {
    ImGui::TextColored(gneiss::editor::theme_success_color(), "Running");
  } else if (runtime.started) {
    ImGui::Text("Exited with code %d", runtime.exit_code);
  } else {
    ImGui::TextDisabled("Runtime has not started");
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear") && actions.clear != nullptr) {
    actions.clear(actions.context);
  }
  ImGui::SameLine();
  if (ImGui::Button(state.paused ? "Resume" : "Pause")) {
    state.set_paused(model, !state.paused);
  }
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &state.auto_scroll);
  if (runtime.attempted && runtime.operation != gneiss::result::success) {
    const auto message = runtime.operation.message();
    ImGui::TextColored(gneiss::editor::theme_error_color(), "Runtime error: %.*s",
                       static_cast<int>(message.size()), message.data());
  }
  if (!runtime.log_path.empty()) {
    ImGui::TextDisabled("Log: %.*s", static_cast<int>(runtime.log_path.size()),
                        runtime.log_path.data());
  }
}

void draw_console_filters(console_panel_state& state) {
  auto severity_toggle = [&state](const char* label, std::uint32_t severity) {
    auto enabled = (state.filter.severity_mask & (UINT32_C(1) << severity)) != 0U;
    if (ImGui::Checkbox(label, &enabled)) {
      if (enabled) {
        state.filter.severity_mask |= UINT32_C(1) << severity;
      } else {
        state.filter.severity_mask &= ~(UINT32_C(1) << severity);
      }
    }
  };
  severity_toggle("Trace", GNEISS_LOG_TRACE);
  ImGui::SameLine();
  severity_toggle("Debug", GNEISS_LOG_DEBUG);
  ImGui::SameLine();
  severity_toggle("Info", GNEISS_LOG_INFO);
  ImGui::SameLine();
  severity_toggle("Warn", GNEISS_LOG_WARNING);
  ImGui::SameLine();
  severity_toggle("Error", GNEISS_LOG_ERROR);
  ImGui::SameLine();
  severity_toggle("Fatal", GNEISS_LOG_FATAL);
  ImGui::SameLine();
  ImGui::Checkbox("Raw", &state.filter.include_raw);
  ImGui::SameLine();
  ImGui::Checkbox("Current session", &state.filter.current_session_only);

  ImGui::SetNextItemWidth(220.0F);
  ImGui::InputTextWithHint("##ConsoleSearch", "Search", state.search.data(), state.search.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150.0F);
  ImGui::InputTextWithHint("##ConsoleSource", "Source (exact)", state.source.data(),
                           state.source.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150.0F);
  ImGui::InputTextWithHint("##ConsoleCategory", "Category (exact)", state.category.data(),
                           state.category.size());
}

void draw_console_lines(const console_model& model, const console_panel_state& state,
                        const std::vector<std::size_t>& visible) {
  ImGui::BeginChild("ConsoleLog", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible.size()));
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const auto& entry = model.entries()[visible[static_cast<std::size_t>(row)]];
      const auto line = format_console_entry(entry);
      if (entry.kind == gneiss::editor::console_entry_kind::structured &&
          entry.event.severity >= GNEISS_LOG_ERROR) {
        ImGui::TextColored(gneiss::editor::theme_error_color(), "%s", line.c_str());
      } else if (entry.kind == gneiss::editor::console_entry_kind::structured &&
                 entry.event.severity == GNEISS_LOG_WARNING) {
        ImGui::TextColored(gneiss::editor::theme_warning_color(), "%s", line.c_str());
      } else {
        ImGui::TextUnformatted(line.c_str());
      }
    }
  }
  if (state.auto_scroll && !state.paused) {
    ImGui::SetScrollHereY(1.0F);
  }
  ImGui::EndChild();
}
} // namespace

void draw_console_panel(const console_model& model, console_panel_state& state, bool& is_open,
                        const console_runtime_view& runtime, const console_panel_actions& actions) {
  ImGui::SetNextWindowSizeConstraints(ImVec2(320.0F, 160.0F), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::Begin("Console", &is_open)) {
    draw_console_toolbar(model, state, runtime, actions);
    draw_console_filters(state);

    std::vector<std::size_t> visible;
    const auto filter_result = state.visible_indices(model, visible);
    if (filter_result != gneiss::result::success) {
      ImGui::TextColored(gneiss::editor::theme_error_color(), "Console filter failed");
    }
    ImGui::TextDisabled("Visible: %zu / %zu | Dropped: %llu", visible.size(),
                        model.entries().size(),
                        static_cast<unsigned long long>(model.dropped_count()));
    ImGui::SameLine();
    if (ImGui::Button("Copy visible")) {
      std::string clipboard;
      for (const auto index : visible) {
        clipboard.append(format_console_entry(model.entries()[index]));
        clipboard.push_back('\n');
      }
      ImGui::SetClipboardText(clipboard.c_str());
    }
    ImGui::Separator();
    draw_console_lines(model, state, visible);
  }
  ImGui::End();
}

} // namespace gneiss::editor
