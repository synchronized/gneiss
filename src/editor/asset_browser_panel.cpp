// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_browser_panel.hpp"

#include "editor_theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>

namespace gneiss::editor {
namespace {
const char* text_data(std::string_view text) noexcept { return text.empty() ? "" : text.data(); }
void wrapped(std::string_view text) {
  ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text_data(text));
}
ImVec4 status_color(asset_status_tone tone) {
  switch (tone) {
  case asset_status_tone::success:
    return theme_success_color();
  case asset_status_tone::error:
    return theme_error_color();
  default:
    return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
  }
}
void error_text(const char* prefix, result operation) {
  const auto message = operation.message();
  ImGui::TextColored(theme_error_color(), "%s: %.*s", prefix, static_cast<int>(message.size()),
                     text_data(message));
}
[[nodiscard]] const char* asset_kind_name(asset_browser_kind kind) {
  switch (kind) {
  case asset_browser_kind::source:
    return "SRC";
  case asset_browser_kind::authored_asset:
    return "ASSET";
  case asset_browser_kind::imported_output:
    return "GEN";
  }
  return "?";
}

[[nodiscard]] const char* asset_status_name(asset_browser_status status) {
  switch (status) {
  case asset_browser_status::untracked:
    return "Untracked";
  case asset_browser_status::ready:
    return "Ready";
  case asset_browser_status::stale:
    return "Stale";
  case asset_browser_status::missing:
    return "Missing";
  }
  return "Unknown";
}

struct panel_buttons final {
  asset_browser_request& request;
  const asset_browser_model& model;

  void operator()(const char* label, asset_browser_action action, bool enabled = true) const {
    ImGui::BeginDisabled(!enabled);
    const auto pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    if (pressed) {
      request = {.action = action, .asset_id = model.selection()};
    }
  }
};

void draw_watch_status(const asset_browser_view& view, const panel_buttons& button) {
  for (std::size_t index = 0U; index < view.watches.size(); ++index) {
    const auto& watch = view.watches[index];
    const bool source = index == 0U;
    const auto* label = source ? "Source watch" : "Author asset watch";
    ImGui::PushID(label);
    if (watch.waiting) {
      ImGui::TextDisabled("Source watch: waiting for sources directory");
    } else {
      if (watch.operation != result::success) {
        const auto message = watch.operation.message();
        ImGui::TextColored(theme_error_color(), "%s (%.*s): %.*s", label,
                           static_cast<int>(watch.root.size()), text_data(watch.root),
                           static_cast<int>(message.size()), text_data(message));
        button("Restart watch", source ? asset_browser_action::restart_source_watch
                                       : asset_browser_action::restart_author_watch);
      }
      if (watch.dropped != 0U) {
        ImGui::TextColored(theme_error_color(), "%s: %llu events lost", label,
                           static_cast<unsigned long long>(watch.dropped));
        wrapped(
            source ? "Indexed sources are checked automatically; review import failures below."
                   : "Author assets are checked automatically; review unsaved document conflicts.");
      }
    }
    ImGui::PopID();
  }
  if (view.source_rescanning) {
    ImGui::TextDisabled("Checking indexed sources...");
  } else if (view.source_scan_result != result::success) {
    error_text("Source check failed", view.source_scan_result);
  }
  if (view.author_rescanning) {
    ImGui::TextDisabled("Checking author assets...");
  } else if (view.author_scan_result != result::success) {
    error_text("Author asset check failed", view.author_scan_result);
  }
}

void draw_import_status(const asset_browser_view& view, const panel_buttons& button,
                        asset_browser_model& model, ImGuiTextFilter& filter) {
  button("Check author assets", asset_browser_action::scan_author_assets);
  button("Check indexed sources", asset_browser_action::scan_sources);
  button("Refresh", asset_browser_action::refresh);
  ImGui::SameLine();
  button("Import...", asset_browser_action::import_asset);
  const auto selected =
      std::ranges::find(model.entries(), model.selection(), &asset_browser_entry::id);
  ImGui::SameLine();
  button("Reimport", asset_browser_action::reimport_selected,
         selected != model.entries().end() && selected->kind == asset_browser_kind::source);
  ImGui::Text("Assets: %.*s | queued: %zu", static_cast<int>(view.worker_stage.size()),
              text_data(view.worker_stage), view.queued);
  if (!view.worker_source.empty()) {
    wrapped(view.worker_source);
  }
  if (!view.worker_error.empty()) {
    ImGui::TextColored(theme_error_color(), "%.*s", static_cast<int>(view.worker_error.size()),
                       text_data(view.worker_error));
  }
  if (view.can_cancel_tasks) {
    button("Cancel asset tasks", asset_browser_action::cancel_tasks);
  }
  if (view.can_retry_import) {
    button("Retry last import", asset_browser_action::retry_import);
  }
  filter.Draw("Filter", -1.0F);
  if (view.refresh_failed) {
    ImGui::TextColored(theme_error_color(), "Refresh failed: %s", model.diagnostic().c_str());
  }
  if (view.import_attempted) {
    if (view.import_succeeded) {
      ImGui::TextColored(theme_success_color(), "Import succeeded");
    } else {
      ImGui::TextColored(theme_error_color(), "Import failed: %.*s",
                         static_cast<int>(view.import_diagnostic.size()),
                         text_data(view.import_diagnostic));
    }
  }
}

void draw_scene_status(const asset_browser_view& view, const panel_buttons& button) {
  const auto& scene = view.scene;
  if (scene.visible) {
    ImGui::Text("Runtime scene: %.*s", static_cast<int>(scene.phase.size()),
                text_data(scene.phase));
    wrapped(scene.uri);
    if (scene.total > 0U) {
      ImGui::Text("Current phase: %u / %u", scene.completed, scene.total);
    }
    if (scene.budget) {
      const auto& budget = *scene.budget;
      constexpr double mib = 1024.0 * 1024.0;
      ImGui::Text("%s: logical %.1f MiB, CPU data %.1f MiB",
                  budget.cleanup_complete ? "Candidate before cleanup" : "Candidate",
                  static_cast<double>(budget.candidate_logical) / mib,
                  static_cast<double>(budget.candidate_cpu) / mib);
      ImGui::Text("Application: logical %.1f MiB, CPU data %.1f MiB",
                  static_cast<double>(budget.application_logical) / mib,
                  static_cast<double>(budget.application_cpu) / mib);
      ImGui::Text("Available: %.1f MiB | Upload: %.1f MiB (peak %.1f MiB)",
                  static_cast<double>(budget.available) / mib,
                  static_cast<double>(budget.upload) / mib,
                  static_cast<double>(budget.peak_upload) / mib);
      ImGui::TextDisabled("Resource accounting snapshot; not process RAM or GPU memory");
      if (budget.cleanup_complete) {
        ImGui::TextUnformatted("Request cleanup complete; other live resources may remain");
      }
      if (budget.cleanup_pending) {
        ImGui::TextUnformatted("Cancelled; waiting for resource cleanup");
      }
    }
    if (!scene.message.empty()) {
      wrapped(scene.message);
    }
    if (scene.can_cancel) {
      button("Cancel scene load", asset_browser_action::cancel_scene_load, scene.enabled);
    }
    if (scene.can_retry) {
      button("Retry scene load", asset_browser_action::retry_scene_load, scene.enabled);
    }
  }
  button("Load scene in Runtime", asset_browser_action::load_scene, scene.can_load);
}

void draw_runtime_asset_status(const asset_browser_view& view, const panel_buttons& button) {
  const auto& reload = view.reload;
  if (reload.publish_result != result::success) {
    error_text("Runtime asset publish failed", reload.publish_result);
    wrapped("Retry importing or saving the affected asset.");
  }
  if (reload.visible) {
    ImGui::TextColored(status_color(reload.tone), "Runtime asset revision %llu: %.*s",
                       static_cast<unsigned long long>(reload.revision),
                       static_cast<int>(reload.message.size()), text_data(reload.message));
    if (reload.total > 0U) {
      ImGui::Text("Uploaded: %u / %u", reload.completed, reload.total);
    }
    if (reload.can_cancel) {
      button("Cancel Runtime asset update", asset_browser_action::cancel_runtime_reload);
    }
    if (reload.can_retry) {
      button("Retry Runtime asset sync", asset_browser_action::retry_runtime_reload);
    }
    if (reload.restart_required) {
      wrapped("Stop and restart Runtime to apply these assets.");
    }
  }
  if (view.author_change_visible) {
    ImGui::TextColored(status_color(view.author_change_tone), "%.*s: %.*s",
                       static_cast<int>(view.author_uri.size()), text_data(view.author_uri),
                       static_cast<int>(view.author_message.size()),
                       text_data(view.author_message));
  }
}

} // namespace

asset_browser_request draw_asset_browser_panel(asset_browser_model& model, ImGuiTextFilter& filter,
                                               bool& open, const asset_browser_view& view) {
  asset_browser_request request;
  ImGui::SetNextWindowSizeConstraints({220.0F, 160.0F}, {FLT_MAX, FLT_MAX});
  if (!ImGui::Begin("Asset Browser", &open)) {
    ImGui::End();
    return request;
  }
  const panel_buttons button{.request = request, .model = model};
  draw_watch_status(view, button);
  draw_import_status(view, button, model, filter);
  draw_scene_status(view, button);
  draw_runtime_asset_status(view, button);
  button("Add Mesh", asset_browser_action::add_mesh, view.can_add_mesh);
  ImGui::SameLine();
  button("Add Prefab", asset_browser_action::add_prefab, view.can_add_prefab);
  ImGui::SameLine();
  button("Apply to Node", asset_browser_action::apply_to_node, view.can_apply);
  if (view.scene_edit_attempted && view.scene_edit_result != result::success) {
    error_text("Scene edit failed", view.scene_edit_result);
  }
  ImGui::Separator();
  for (const auto& entry : model.entries()) {
    if (!filter.PassFilter(entry.relative_path.c_str())) {
      continue;
    }
    ImGui::PushID(entry.id.c_str());
    const auto label = std::string{"["} + asset_kind_name(entry.kind) + "] " + entry.display_name;
    if (ImGui::Selectable(label.c_str(), model.selection() == entry.id)) {
      (void)model.select(entry.id);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s\n%s", entry.relative_path.c_str(), asset_status_name(entry.status));
    }
    ImGui::PopID();
  }
  ImGui::End();
  return request;
}

} // namespace gneiss::editor
