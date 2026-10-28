// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include "runtime/application.hpp"
#include "support/value_result.hpp"

#include <array>
#include <memory>

namespace {
struct draw_context {
  gneiss::engine::texture texture;
  unsigned updates = 0;
  unsigned failure = 0;
};

gneiss::engine::error update(gneiss::runtime::application& app, draw_context& context) noexcept {
  ++context.updates;
  const auto submit_ui = [&](const gneiss::engine::ui_draw_view& value) {
    return app.submit_ui_draw_list(value);
  };
  const auto submit_debug = [&](std::span<const gneiss::engine::debug_line> value) {
    return app.submit_debug_draw_list(value);
  };
  // 所有输入均为本次回调的局部存储，不延长至后续帧。
  const std::array vertices{
      gneiss::engine::ui_vertex{.position = {0, 0}, .uv = {0, 0}, .color_rgba8 = 0xffffffffU},
      gneiss::engine::ui_vertex{.position = {1, 0}, .uv = {1, 0}, .color_rgba8 = 0xffffffffU},
      gneiss::engine::ui_vertex{.position = {0, 1}, .uv = {0, 1}, .color_rgba8 = 0xffffffffU},
  };
  std::array<std::uint32_t, 3> indices{0, 1, 2};
  const std::array commands{
      gneiss::engine::ui_draw_command{
          .texture = context.texture,
          .clip_min = {0, 0},
          .clip_max = {32, 32},
          .index_count = 3,
      },
  };
  const gneiss::engine::ui_draw_view ui{
      .display_width = 32,
      .display_height = 32,
      .vertices = vertices,
      .indices = indices,
      .commands = commands,
  };
  if (submit_ui(ui).failed()) {
    context.failure = 1;
    return gneiss::engine::error::internal;
  }
  indices[2] = 9;
  if (submit_ui(ui) != gneiss::engine::error::invalid_argument) {
    context.failure = 2;
    return gneiss::engine::error::internal;
  }
  indices[2] = 2;
  if (submit_ui(ui).failed()) {
    context.failure = 3;
    return gneiss::engine::error::internal;
  }
  std::array lines{
      gneiss::engine::debug_line{
          .start = {0, 0, 0},
          .end = {1, 1, 1},
          .color_rgba8 = 0xff00ffffU,
          .width = 2,
          .depth_test = false,
      },
  };

  if (submit_debug(lines).failed()) {
    context.failure = 4;
    return gneiss::engine::error::internal;
  }
  lines[0].width = -1;
  if (submit_debug(lines) != gneiss::engine::error::invalid_argument || submit_debug({}).failed()) {
    context.failure = 5;
    return gneiss::engine::error::internal;
  }
  return gneiss::engine::error::success;
}
} // namespace

int main() {
  draw_context context;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration config;
  config.callbacks.update = [&](const gneiss::engine::frame_time&) {
    return update(*app, context);
  };
  if (gneiss::runtime::application::create(config, app).failed()) {
    return 1;
  }
  const std::array<std::uint8_t, 4> pixels{255, 255, 255, 255};
  const gneiss::engine::texture_view texture{
      .width = 1, .height = 1, .row_stride_bytes = 4, .pixels = pixels};
  if (gneiss::test_support::take_value(app->resources().create_texture(texture), context.texture)
          .failed()) {
    return 2;
  }
  if (app->submit_debug_draw_list({}) != gneiss::engine::error::invalid_state ||
      app->submit_ui_draw_list({}) != gneiss::engine::error::invalid_state) {
    return 3;
  }
  const auto status = app->run(2);
  if (status.failed() || context.failure != 0) {
    return 10 + static_cast<int>(context.failure);
  }
  if (context.updates != 2) {
    return 4;
  }
  context.texture.reset();
  return app->shutdown().failed() ? 6 : 0;
}
