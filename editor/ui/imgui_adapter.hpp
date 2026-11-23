// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_
#define GNEISS_APPS_EDITOR_IMGUI_ADAPTER_H_

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/frame_time.hpp>
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/function/render/draw_types.hpp>
#include <gneiss/engine/function/render/ui_draw_list.hpp>
#include <gneiss/engine/platform/window/input.hpp>

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace gneiss::app {
class application;
}

namespace gneiss::editor {

class imgui_adapter final {
public:
  imgui_adapter() noexcept = default;
  ~imgui_adapter() noexcept;

  imgui_adapter(const imgui_adapter&) = delete;
  imgui_adapter& operator=(const imgui_adapter&) = delete;

  [[nodiscard]] gneiss::error initialize(app::application& application);
  void shutdown(app::application& application) noexcept;
  [[nodiscard]] gneiss::error begin_frame(app::application& application,
                                          const gneiss::frame_time& time);
  /** 结束 UI 帧并借用绘制数据；有效期至下次 render 或 shutdown，宿主复制进自己的帧。 */
  [[nodiscard]] gneiss::result<gneiss::ui_draw_view> render();

private:
  void process_input(const gneiss::input::event& event);

  ImGuiContext* context_ = nullptr;
  gneiss::ref<const gneiss::texture> font_texture_;
  std::vector<gneiss::ui_vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<gneiss::ui_draw_command> commands_;
};

} // namespace gneiss::editor

#endif
