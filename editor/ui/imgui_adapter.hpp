// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_
#define GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/frame_time.hpp>
#include <gneiss/engine/core/input/input_types.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/function/render/draw_types.hpp>

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace gneiss::runtime {
class application;
}

namespace gneiss::editor {

class imgui_adapter final {
public:
  imgui_adapter() noexcept = default;
  ~imgui_adapter() noexcept;

  imgui_adapter(const imgui_adapter&) = delete;
  imgui_adapter& operator=(const imgui_adapter&) = delete;

  [[nodiscard]] gneiss::error initialize(runtime::application& application);
  void shutdown(runtime::application& application) noexcept;
  [[nodiscard]] gneiss::error begin_frame(runtime::application& application,
                                                  const gneiss::frame_time& time);
  [[nodiscard]] gneiss::error submit(runtime::application& application);

private:
  void process_input(const gneiss::input_event& event);

  ImGuiContext* context_ = nullptr;
  gneiss::ref<const gneiss::texture> font_texture_;
  std::vector<gneiss::ui_vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<gneiss::ui_draw_command> commands_;
};

} // namespace gneiss::editor

#endif
