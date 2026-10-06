// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_
#define GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_

#include <gneiss/application.h>
#include <gneiss/input.h>
#include <gneiss/render.h>

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace gneiss::editor {

class imgui_adapter final {
public:
  imgui_adapter() noexcept = default;
  ~imgui_adapter() noexcept;

  imgui_adapter(const imgui_adapter&) = delete;
  imgui_adapter& operator=(const imgui_adapter&) = delete;

  [[nodiscard]] gneiss_result initialize(gneiss_application application);
  void shutdown(gneiss_application application) noexcept;
  [[nodiscard]] gneiss_result begin_frame(gneiss_application application,
                                          const gneiss_frame_time& time);
  [[nodiscard]] gneiss_result submit(gneiss_application application);

private:
  void process_input(const gneiss_input_event& event);

  ImGuiContext* context_ = nullptr;
  gneiss_texture font_texture_ = GNEISS_NULL_TEXTURE;
  std::vector<gneiss_ui_vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<gneiss_ui_draw_command> commands_;
};

} // namespace gneiss::editor

#endif
