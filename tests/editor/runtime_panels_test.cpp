// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_panels.hpp"

#include <imgui.h>

#include <cstdint>

namespace {
struct observations final {
  std::uint32_t reads = 0U;
  std::uint32_t mutations = 0U;
  bool wrong_object = false;
};
} // namespace

int main() try {
  using namespace gneiss::editor;
  runtime_scene_mirror mirror;
  runtime_scene_change change;
  change.id = {.value = 7U, .generation = 2U};
  change.node.id = change.id;
  change.node.uuid = "root";
  const runtime_scene_batch batch{
      .stamp = {.session_id = 3U, .sequence = 1U}, .is_full = true, .changes = {change}};
  if (mirror.apply(batch).failed()) {
    return 1;
  }
  runtime_scene_selection selection;
  if (selected_runtime_node(mirror, selection) != nullptr ||
      selected_runtime_node(mirror, {.object = change.id, .session = 2U}) != nullptr ||
      selected_runtime_node(mirror, {.object = {.value = 7U, .generation = 1U}, .session = 3U}) !=
          nullptr) {
    return 2;
  }
  selection = {.object = change.id, .session = 3U};
  const auto* node = selected_runtime_node(mirror, selection);
  if (node == nullptr || node != &mirror.nodes().front()) {
    return 3;
  }

  observations observed;
  const runtime_inspector_actions actions{
      .context = &observed,
      .editable = false,
      .can_apply_to_author = false,
      .find_edit = [](void* context,
                      const runtime_property_key& key) -> const runtime_property_edit* {
        auto& counters = *static_cast<observations*>(context);
        ++counters.reads;
        counters.wrong_object |= key.object != runtime_object_id{.value = 7U, .generation = 2U};
        return nullptr;
      },
      // NOLINTNEXTLINE(performance-unnecessary-value-param): 测试桩须匹配转移属性值的回调契约。
      .write_property =
          [](void* context, const runtime_property_key&, std::uint64_t,
             [[maybe_unused]] runtime_property_value value) {
            ++static_cast<observations*>(context)->mutations;
            return gneiss::result::success;
          },
      .apply_to_author =
          [](void* context, const runtime_scene_node&) {
            ++static_cast<observations*>(context)->mutations;
          },
      .report_result = [](void* context,
                          gneiss::result) { ++static_cast<observations*>(context)->mutations; }};
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
  ImGui::SetNextWindowSize({800.0F, 600.0F});
  ImGui::Begin("Runtime");
  draw_runtime_hierarchy(mirror, selection);
  draw_runtime_inspector(*node, actions);
  ImGui::End();
  ImGui::Render();
  const bool drew = ImGui::GetDrawData()->TotalVtxCount > 0;
  ImGui::DestroyContext();
  if (!drew || observed.reads != 3U || observed.mutations != 0U || observed.wrong_object ||
      selection.object != change.id || selection.session != 3U) {
    return 4;
  }
  mirror.reset();
  return selected_runtime_node(mirror, selection) == nullptr ? 0 : 5;
} catch (...) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
  return 6;
}
