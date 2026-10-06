// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.hpp>

#include <array>
#include <type_traits>

namespace {
struct draw_context {
  gneiss::texture_id texture;
  unsigned updates = 0;
  unsigned failure = 0;
};

gneiss::result update(gneiss::application_ref app, const gneiss::frame_time& /*unused*/,
                      void* data) noexcept {
  auto& context = *static_cast<draw_context*>(data);
  ++context.updates;
  // 所有输入均为本次回调的局部存储，不延长至后续帧。
  const std::array vertices{
      gneiss::ui_vertex{.position = {0, 0}, .uv = {0, 0}, .color_rgba8 = 0xffffffffU},
      gneiss::ui_vertex{.position = {1, 0}, .uv = {1, 0}, .color_rgba8 = 0xffffffffU},
      gneiss::ui_vertex{.position = {0, 1}, .uv = {0, 1}, .color_rgba8 = 0xffffffffU},
  };
  std::array<std::uint32_t, 3> indices{0, 1, 2};
  const std::array commands{
      gneiss::ui_draw_command{
          .texture = context.texture,
          .clip_min = {0, 0},
          .clip_max = {32, 32},
          .index_count = 3,
      },
  };
  const gneiss::ui_draw_list_desc ui{
      .display_width = 32,
      .display_height = 32,
      .vertices = vertices,
      .indices = indices,
      .commands = commands,
  };
  if (app.submit_ui_draw_list(ui).failed()) {
    context.failure = 1;
    return gneiss::result::internal;
  }
  indices[2] = 9;
  if (app.submit_ui_draw_list(ui) != gneiss::result::invalid_argument) {
    context.failure = 2;
    return gneiss::result::internal;
  }
  indices[2] = 2;
  if (app.submit_ui_draw_list(ui).failed()) {
    context.failure = 3;
    return gneiss::result::internal;
  }
  std::array lines{
      gneiss::debug_line{
          .start = {0, 0, 0},
          .end = {1, 1, 1},
          .color_rgba8 = 0xff00ffffU,
          .width = 2,
          .depth_test = false,
      },
  };
  const gneiss::debug_draw_list_desc debug{.lines = lines};
  if (app.submit_debug_draw_list(debug).failed()) {
    context.failure = 4;
    return gneiss::result::internal;
  }
  lines[0].width = -1;
  if (app.submit_debug_draw_list(debug) != gneiss::result::invalid_argument ||
      app.submit_debug_draw_list(gneiss::debug_draw_list_desc{}).failed()) {
    context.failure = 5;
    return gneiss::result::internal;
  }
  return gneiss::result::success;
}
} // namespace

int main() {
  static_assert(!std::is_same_v<gneiss::ui_vertex, gneiss_ui_vertex>);
  static_assert(!std::is_same_v<gneiss::debug_line, gneiss_debug_line>);
  draw_context context;
  gneiss::application_desc desc{};
  desc.callbacks.user_data = &context;
  desc.callbacks.update = update;
  gneiss::application app;
  if (gneiss::application::create(desc, app).failed()) {
    return 1;
  }
  const std::array<std::uint8_t, 4> pixels{255, 255, 255, 255};
  const gneiss::texture_desc texture_desc{
      .width = 1,
      .height = 1,
      .row_stride_bytes = 4,
      .pixels = pixels,
  };
  gneiss::texture texture;
  if (app.create_texture(texture_desc, texture).failed()) {
    return 2;
  }
  context.texture = texture.id();
  if (app.submit_debug_draw_list(gneiss::debug_draw_list_desc{}) != gneiss::result::invalid_state ||
      app.submit_ui_draw_list(gneiss::ui_draw_list_desc{}) != gneiss::result::invalid_state) {
    return 3;
  }
  const auto status = app.run(2);
  if (status.failed() || context.failure != 0) {
    return 10 + static_cast<int>(context.failure);
  }
  if (context.updates != 2) {
    return 4;
  }
  return 0;
}
