// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/resource/texture/texture_view.hpp>

#include <array>
#include <memory>

namespace {
struct draw_context {
  gneiss::ref<const gneiss::texture> texture;
  gneiss::ui_draw_list ui;
  gneiss::debug_draw_list debug;
  unsigned updates = 0;
  unsigned failure = 0;
};

gneiss::error update(gneiss::runtime::application& app, draw_context& context) noexcept {
  ++context.updates;
  const auto submit_ui = [&](const gneiss::ui_draw_view& value) {
    return context.ui.replace(value, app.render());
  };
  const auto submit_debug = [&](std::span<const gneiss::debug_line> value) {
    return context.debug.replace(value);
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
  const gneiss::ui_draw_view ui{
      .display_width = 32,
      .display_height = 32,
      .vertices = vertices,
      .indices = indices,
      .commands = commands,
  };
  if (submit_ui(ui).failed()) {
    context.failure = 1;
    return gneiss::error::internal;
  }
  indices[2] = 9;
  if (submit_ui(ui) != gneiss::error::invalid_argument) {
    context.failure = 2;
    return gneiss::error::internal;
  }
  indices[2] = 2;
  if (submit_ui(ui).failed()) {
    context.failure = 3;
    return gneiss::error::internal;
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

  if (submit_debug(lines).failed()) {
    context.failure = 4;
    return gneiss::error::internal;
  }
  lines[0].width = -1;
  if (submit_debug(lines) != gneiss::error::invalid_argument || submit_debug({}).failed()) {
    context.failure = 5;
    return gneiss::error::internal;
  }
  return gneiss::error::success;
}
} // namespace

int main() {
  draw_context context;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration config;
  config.callbacks.update = [&](const gneiss::frame_time&) { return update(*app, context); };
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app)
          .failed()) {
    return 1;
  }
  const std::array<std::uint8_t, 4> pixels{255, 255, 255, 255};
  const gneiss::texture_view texture{
      .width = 1, .height = 1, .row_stride_bytes = 4, .pixels = pixels};
  if (gneiss::test_support::take_value(app->assets().create(texture), context.texture).failed()) {
    return 2;
  }
  const auto status = app->run(2);
  if (status.failed() || context.failure != 0) {
    return 10 + static_cast<int>(context.failure);
  }
  if (context.updates != 2) {
    return 4;
  }
  context.ui.clear();
  context.debug.clear();
  context.texture.reset();
  return app->shutdown().failed() ? 6 : 0;
}
