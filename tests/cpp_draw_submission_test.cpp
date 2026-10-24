// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/values/render.hpp"
#include "support/c_owner.hpp"

#include <array>
#include <type_traits>

namespace {
struct draw_context {
  gneiss::texture_id texture;
  unsigned updates = 0;
  unsigned failure = 0;
};

gneiss_result update(gneiss_application app, const gneiss_frame_time* /*unused*/,
                     void* data) noexcept {
  auto& context = *static_cast<draw_context*>(data);
  ++context.updates;
  const auto submit_ui = [&](const gneiss::ui_draw_list_desc& value) {
    return gneiss::detail::with_render_desc(value, [&](const auto& native) {
      return gneiss_application_submit_ui_draw_list(app, &native);
    });
  };
  const auto submit_debug = [&](const gneiss::debug_draw_list_desc& value) {
    return gneiss::detail::with_render_desc(value, [&](const auto& native) {
      return gneiss_application_submit_debug_draw_list(app, &native);
    });
  };
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
  if (submit_ui(ui).failed()) {
    context.failure = 1;
    return GNEISS_ERROR_INTERNAL;
  }
  indices[2] = 9;
  if (submit_ui(ui) != gneiss::result::invalid_argument) {
    context.failure = 2;
    return GNEISS_ERROR_INTERNAL;
  }
  indices[2] = 2;
  if (submit_ui(ui).failed()) {
    context.failure = 3;
    return GNEISS_ERROR_INTERNAL;
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
  if (submit_debug(debug).failed()) {
    context.failure = 4;
    return GNEISS_ERROR_INTERNAL;
  }
  lines[0].width = -1;
  if (submit_debug(debug) != gneiss::result::invalid_argument ||
      submit_debug(gneiss::debug_draw_list_desc{}).failed()) {
    context.failure = 5;
    return GNEISS_ERROR_INTERNAL;
  }
  return GNEISS_SUCCESS;
}
} // namespace

int main() {
  static_assert(!std::is_same_v<gneiss::ui_vertex, gneiss_ui_vertex>);
  static_assert(!std::is_same_v<gneiss::debug_line, gneiss_debug_line>);
  draw_context context;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &context;
  desc.update = update;
  gneiss::test_support::application_owner app;
  if (gneiss_application_create(&desc, app.put()) != GNEISS_SUCCESS) {
    return 1;
  }
  const std::array<std::uint8_t, 4> pixels{255, 255, 255, 255};
  const gneiss::texture_desc texture_desc{
      .width = 1,
      .height = 1,
      .row_stride_bytes = 4,
      .pixels = pixels,
  };
  gneiss_texture texture{};
  if (gneiss::detail::with_render_desc(texture_desc, [&](const auto& native) {
        return gneiss_texture_create(app.get(), &native, &texture);
      }).failed()) {
    return 2;
  }
  context.texture = gneiss::texture_id{texture};
  gneiss_debug_draw_list_desc empty_debug = GNEISS_DEBUG_DRAW_LIST_DESC_INIT;
  gneiss_ui_draw_list_desc empty_ui = GNEISS_UI_DRAW_LIST_DESC_INIT;
  if (gneiss_application_submit_debug_draw_list(app.get(), &empty_debug) !=
          GNEISS_ERROR_INVALID_STATE ||
      gneiss_application_submit_ui_draw_list(app.get(), &empty_ui) != GNEISS_ERROR_INVALID_STATE) {
    return 3;
  }
  const auto status = gneiss_application_run(app.get(), 2);
  if (status != GNEISS_SUCCESS || context.failure != 0) {
    return 10 + static_cast<int>(context.failure);
  }
  if (context.updates != 2) {
    return 4;
  }
  return gneiss_texture_destroy(app.get(), texture) == GNEISS_SUCCESS ? 0 : 5;
}
