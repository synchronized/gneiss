// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/editor_scene_loader.hpp"
#include "ui/viewport/scene_picking.hpp"
#include "viewport/scene_viewport.hpp"
#include <chrono>
#include <cstdio>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/rendering/render_camera.hpp>
#include <stdexcept>
#include <thread>

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  const auto check = [](bool ok) {
    if (!ok)
      throw std::runtime_error("拾取验证失败");
  };
  tasks::scheduler scheduler({.mode = tasks::execution_mode::cooperative});
  app::configuration config;
  config.asset_root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  config.executor = &scheduler;
  config.enable_window = true;
  config.window.visible = false;
  config.window.width = config.window.height = 64;
  auto application = app::application::create(config);
  check(bool(application));
  auto& app = **application;
  auto created = create_native();
  check(bool(created));
  auto world = std::move(*created);
  document_editing editing;
  auto& session = editing.document();
  editor_camera camera;
  editor_scene_loader loader;
  check(camera.initialize().ok() && app.step());
  check(loader.request(app, editing, "asset://scenes/triangle.scene.json").ok());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (loader.active() || loader.visual_loading()) {
    check(std::chrono::steady_clock::now() < deadline);
    (void)scheduler.run_ready();
    check(bool(app.step()));
    bool changed{};
    check(loader.advance(app, editing, *world, camera, changed).ok());
    std::this_thread::yield();
  }
  check(loader.operation().ok());
  auto bound = world_render_binding::create(*world, app.render());
  check(bool(bound));
  auto binding = std::move(*bound);
  world->flush_changes();
  check(binding->synchronize().ok());
  view_camera eye;
  eye.transform.translation.z = 4;
  auto selected = select_render_camera(*world, 64, 64, &eye);
  check(bool(selected) && bool(*selected));
  render::view view{.scene = binding->scene(),
                    .view_matrix = (**selected).view,
                    .projection_matrix = (**selected).projection,
                    .camera_position = eye.transform.translation};
  const render::target_description extent{64, 64};
  scene_picking picking;
  const auto update = [&] {
    check(picking.update(app.render(), *binding, session, view, extent).ok());
  };
  const auto complete = [&] {
    update();
    check(app.render().finish_frames().ok());
    update();
    check(!picking.active());
  };
  check(session.select({}).ok());
  const auto version = session.current_version().stamp();
  const auto history_size = editing.history().size();
  picking.queue(session, {}, extent, {.5F, .5F});
  complete();
  check(bool(session.selection()));
  const auto hit = session.selection();
  check(session.selected_node()->uuid == guid::literal("00000000-0000-4000-8000-000000000003"));
  check(session.current_version().stamp() == version && editing.history().size() == history_size);
  picking.queue(session, {}, extent, {.01F, .01F});
  complete();
  check(!session.selection());
  // 后来的背景点击覆盖尚未领取的命中，不能被旧结果反选。
  picking.queue(session, {}, extent, {.5F, .5F});
  update();
  picking.queue(session, {}, extent, {.01F, .01F});
  complete();
  check(!session.selection());
  picking.queue(session, {}, extent, {1.F, .5F});
  check(!picking.active());
  // 异步完成期间外部选择、相机、尺寸、文档版本变化均使请求作废。
  picking.queue(session, {}, extent, {.01F, .01F});
  update();
  check(session.select(hit).ok());
  complete();
  check(session.selection() == hit);
  picking.queue(session, {}, extent, {.01F, .01F});
  update();
  view.camera_position.x += 1;
  complete();
  check(session.selection() == hit);
  picking.queue(session, {}, extent, {.01F, .01F});
  update();
  check(picking.update(app.render(), *binding, session, view, {32, 64}).ok());
  check(!picking.active() && session.selection() == hit);
  picking.queue(session, {}, extent, {.01F, .01F});
  update();
  check(session.rename_node(hit, "Renamed").ok());
  complete();
  check(session.selection() == hit);
  // 真实视口消费者：目标缩放、相机取景、Gizmo 优先与关闭释放。
  scene_viewport viewport;
  check(viewport.initialize().ok());
  check(viewport.resize(app.render(), extent).ok());
  const auto first_target = viewport.target();
  check(app.render().is_target(first_target));
  check(viewport.update_camera({}, true, editing, *world).ok());
  auto displayed = viewport.view(*world, binding->scene(), extent);
  check(bool(displayed) && bool(*displayed) && (**displayed).target == first_target);
  viewport.select_at(session, {.01F, .01F}, true);
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(app.render().finish_frames().ok());
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(session.selection() == hit);
  viewport.select_at(session, {.01F, .01F}, false);
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(app.render().finish_frames().ok());
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(!session.selection());
  viewport.select_at(session, {.5F, .5F}, false);
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(app.render().finish_frames().ok());
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(session.selection() == hit);
  viewport.select_at(session, {.01F, .01F}, false);
  check(viewport.resize(app.render(), {128, 64}).ok());
  const auto second_target = viewport.target();
  check(second_target != first_target && viewport.extent().width == 128);
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(app.render().finish_frames().ok());
  check(viewport.synchronize(app.render(), *binding, editing, *world).ok());
  check(session.selection() == hit);
  check(viewport.close(app.render()).ok());
  check(!app.render().is_target(first_target) && !app.render().is_target(second_target));
  check(viewport.close(app.render()).ok());
  picking.queue(session, {}, extent, {.5F, .5F});
  update();
  session.close();
  complete();
  binding.reset();
  std::puts("Editor picking: hit/background/latest/stale/close passed");
  return 0;
} catch (const std::exception& exception) {
  std::fprintf(stderr, "%s\n", exception.what());
  return 1;
}
