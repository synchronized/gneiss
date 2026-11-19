// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/rendering/render_camera.hpp>
#include <gneiss/engine/function/scene/rendering/world_render_binding.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/platform/window/service.hpp>

namespace gneiss::test_support {
// GPU 测试共同的单视图装配；World、绑定、目标及服务均由测试显式持有。
inline result<render::frame_request> scene_frame(world& world, world_render_binding& binding,
                                                 window::surface_state surface,
                                                 render::target_description extent = {}) {
  if (!extent.width || !extent.height)
    extent = {surface.width, surface.height};
  render::frame_request frame;
  frame.surface = surface;
  if (!extent.width || !extent.height)
    return frame;
  auto camera = select_render_camera(world, extent.width, extent.height);
  if (!camera)
    return camera.error();
  if (*camera) {
    const auto& selected = **camera;
    frame.views.push_back({.scene = binding.scene(),
                           .view_matrix = selected.view,
                           .projection_matrix = selected.projection,
                           .camera_position = selected.transform.translation});
  } else {
    frame.views.push_back({});
  }
  return frame;
}

// 借用参数必须活到最后一次 step/run；此函数不持有资源、不参与关闭。
inline void add_scene_render_systems(runtime::application& app, world& world,
                                     world_render_binding& binding) {
  app.add_system(app::stage::synchronize, "test.scene_sync", [&] {
    world.flush_changes();
    return binding.synchronize();
  });
  app.add_system(app::stage::present, "test.scene_present", [&]() -> result<void> {
    auto frame = scene_frame(world, binding, app.window()->surface());
    if (!frame)
      return frame.error();
    if (frame->views.empty())
      return {};
    auto status = app.render().render(std::move(*frame));
    return !status && status.error() != error::not_ready ? status : result<void>{};
  });
}

// 只供像素验收等待 GPU 回读；生产宿主直接消费异步完成回执。
inline error capture_frame(render::service& renderer, render::frame_request&& frame,
                           render::target_description extent, render::frame_image& output) {
  auto image = std::make_shared<render::frame_image>();
  auto submitted = renderer.request_capture(std::move(frame), image, extent);
  if (!submitted)
    return submitted.error();
  auto finished = renderer.finish_frames();
  if (finished.failed())
    return finished;
  auto completed = renderer.try_take_required_completion();
  if (!completed)
    return completed.error();
  if (!*completed || (*completed)->sequence != *submitted)
    return error::internal;
  if ((*completed)->status.failed())
    return (*completed)->status;
  output = std::move(*image);
  return error::success;
}
// 只供像素验收等待 GPU 回读；生产宿主直接消费异步完成回执。
inline error capture_scene(runtime::application& app, world& world, world_render_binding& binding,
                           std::uint32_t width, std::uint32_t height, render::frame_image& output) {
  world.flush_changes();
  auto synced = binding.synchronize();
  if (!synced)
    return synced.error();
  auto frame = scene_frame(world, binding, app.window()->surface(), {width, height});
  if (!frame)
    return frame.error();
  return capture_frame(app.render(), std::move(*frame), {width, height}, output);
}
}
