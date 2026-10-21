// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_
#define GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_

#include "engine/core/frame_time.hpp"
#include "engine/core/input/input_types.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/draw_types.hpp"
#include <gneiss/engine/core/result.h>
#include <gneiss/engine/input.h>

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

  [[nodiscard]] gneiss_result initialize(runtime::application& application);
  void shutdown(runtime::application& application) noexcept;
  [[nodiscard]] gneiss_result begin_frame(runtime::application& application,
                                          const gneiss::engine::frame_time& time);
  [[nodiscard]] gneiss_result submit(runtime::application& application);

private:
  void process_input(const input_internal::input_event& event);

  ImGuiContext* context_ = nullptr;
  core::rid font_texture_{};
  std::vector<render_internal::ui_vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<render_internal::ui_draw_command> commands_;
};

} // namespace gneiss::editor

#endif
