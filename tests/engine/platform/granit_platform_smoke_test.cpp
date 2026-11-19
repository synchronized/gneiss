// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/render_scene.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/platform/window/service.hpp>
#include <gneiss/engine/resource/texture/texture_view.hpp>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

#include <array>
#include <cstdio>
#include <string_view>

using namespace gneiss;
namespace native = gneiss;

namespace {

struct smoke_context final {
  gneiss::ui_draw_list ui;
  gneiss::debug_draw_list debug;
  native::ref<const native::texture> texture;
  bool received_key{};
  gneiss::window::id window_id{};
};

native::error submit_ui(gneiss::runtime::application& application, smoke_context& context) {
  gneiss::input::event event;
  while (application.input().poll(event) == native::error::success) {
    // USB HID 物理键编号：A 为 4。
    if (event.type == gneiss::input::event_type::key && event.data.key.physical_key == 4U &&
        event.data.key.action == gneiss::input::key_action::pressed) {
      if (!event.window_id || (context.window_id && context.window_id != event.window_id))
        return error::internal;
      context.window_id = event.window_id;
      context.received_key = true;
    }
  }
  constexpr std::array vertices{
      native::ui_vertex{{16.0F, 16.0F}, {0.0F, 0.0F}, UINT32_C(0xccffffff)},
      native::ui_vertex{{144.0F, 16.0F}, {1.0F, 0.0F}, UINT32_C(0xccffffff)},
      native::ui_vertex{{144.0F, 64.0F}, {1.0F, 1.0F}, UINT32_C(0xccffffff)},
      native::ui_vertex{{16.0F, 64.0F}, {0.0F, 1.0F}, UINT32_C(0xccffffff)}};
  constexpr std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 2U, 3U, 0U};
  const std::array commands{
      native::ui_draw_command{.texture = context.texture,
                              .clip_min = {16.0F, 16.0F},
                              .clip_max = {144.0F, 64.0F},
                              .first_index = 0U,
                              .index_count = static_cast<std::uint32_t>(indices.size()),
                              .vertex_offset = 0U}};
  const native::ui_draw_view draw_list{.display_width = 320.0F,
                                       .display_height = 240.0F,
                                       .vertices = vertices,
                                       .indices = indices,
                                       .commands = commands};
  auto result = context.ui.replace(draw_list, application.render());
  constexpr std::array lines{native::debug_line{.start = {-0.8F, 0.0F, 0.0F},
                                                .end = {0.8F, 0.0F, 0.0F},
                                                .color_rgba8 = UINT32_C(0xff60a5fa),
                                                .width = 2.0F,
                                                .depth_test = true}};
  if (result == native::error::success) {
    result = context.debug.replace(lines);
  }
  return result;
}

} // namespace

int main() {
  constexpr std::string_view title = "Gneiss Granit Platform Smoke Test";
  smoke_context context;
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::runtime::application_configuration config;
  config.enable_window = true;
  config.window = {.title = title, .width = 320, .height = 240};
  const auto create_result =
      gneiss::test_support::assign_value(gneiss::runtime::application::create(config), application);
  if (create_result.failed()) {
    std::fprintf(stderr, "Application 创建失败：%d\n", create_result.code());
    return 1;
  }
  application->add_system(
      gneiss::app::stage::update, "smoke.update",
      [&](const native::frame_time&) { return submit_ui(*application, context); });
  auto owned_world = gneiss::create_native();
  if (!owned_world)
    return 1;
  auto& world = **owned_world;
  auto binding = world_render_binding::create(world, application->render());
  if (!binding)
    return 1;
  application->add_system(app::stage::synchronize, "smoke.sync", [&] {
    world.flush_changes();
    return (*binding)->synchronize();
  });
  const auto make_frame = [&](render::target_description extent = {}) {
    auto frame =
        test_support::scene_frame(world, **binding, application->window()->surface(), extent);
    if (frame) {
      frame->ui = context.ui;
      frame->debug = context.debug;
    }
    return frame;
  };
  application->add_system(app::stage::present, "smoke.present", [&]() -> result<void> {
    auto frame = make_frame();
    if (!frame)
      return frame.error();
    if (frame->views.empty())
      return {};
    auto submitted = application->render().render(std::move(*frame));
    return !submitted && submitted.error() != error::not_ready ? submitted : result<void>{};
  });
  const auto capture = [&](std::uint32_t width, std::uint32_t height,
                           render::frame_image& image) -> error {
    auto frame = make_frame({width, height});
    if (!frame)
      return frame.error();
    return test_support::capture_frame(application->render(), std::move(*frame), {width, height},
                                       image);
  };
  auto& resources = application->render();
  auto& resources_factory = application->assets();
  constexpr std::array vertices{
      native::mesh_vertex{.position = {-0.6F, -0.5F, 0.0F}, .uv = {0.0F, 0.0F}},
      native::mesh_vertex{.position = {0.6F, -0.5F, 0.0F}, .uv = {1.0F, 0.0F}},
      native::mesh_vertex{.position = {0.0F, 0.6F, 0.0F}, .uv = {0.5F, 1.0F}}};
  native::mesh_view mesh_desc{};
  mesh_desc.vertices = vertices;
  native::material_data material_desc{};
  material_desc.parameters.color[0] = 0.95F;
  material_desc.parameters.color[1] = 0.35F;
  material_desc.parameters.color[2] = 0.12F;
  constexpr std::array<std::uint8_t, 16> pixels{255, 255, 255, 255, 32,  64,  255, 255,
                                                32,  64,  255, 255, 255, 255, 255, 255};
  native::texture_view texture_desc{};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.row_stride_bytes = 8;
  texture_desc.pixels = pixels;
  native::rid mesh{}, material{}, plain_material{}, texture{};
  native::ref<const native::mesh> mesh_owner;
  native::ref<const native::material> material_owner, plain_material_owner;
  if (gneiss::test_support::take_value(resources_factory.create(texture_desc), context.texture)
          .failed()) {
    std::fprintf(stderr, "测试 Texture 创建失败\n");
    return 2;
  }
  texture = resource_handle(context.texture);
#ifdef _WIN32
  // 投递真实窗口消息，验证显式事件泵送及原生输入事件传递。
  const auto window = FindWindowW(nullptr, L"Gneiss Granit Platform Smoke Test");
  if (window == nullptr || !PostMessageW(window, WM_KEYDOWN, 'A', 0x001e0001) ||
      !PostMessageW(window, WM_KEYUP, 'A', static_cast<LPARAM>(0xc01e0001U))) {
    return 9;
  }
#endif
  // 先验证没有相机和场景物体的 UI 帧，再验证常规场景及资源更新。
  if (!application->step() || !application->step()) {
    std::fprintf(stderr, "纯 UI 帧运行失败\n");
    return 10;
  }
#ifdef _WIN32
  if (!context.received_key) {
    std::fprintf(stderr, "窗口按键消息未到达 Application\n");
    return 11;
  }
#endif
  if (gneiss::test_support::take_value(resources_factory.create(mesh_desc), mesh_owner).failed() ||
      gneiss::test_support::take_value(resources_factory.create(material_desc.parameters, {}),
                                       plain_material_owner)
          .failed()) {
    std::fprintf(stderr, "测试基础资源创建失败\n");
    return 2;
  }
  mesh = resource_handle(mesh_owner);
  plain_material = resource_handle(plain_material_owner);
  material_desc.base_color_texture = texture;
  if (gneiss::test_support::take_value(
          resources_factory.create(material_desc.parameters, {context.texture}), material_owner)
          .failed()) {
    std::fprintf(stderr, "测试资源创建失败\n");
    return 2;
  }
  material = resource_handle(material_owner);
  entity_id camera_entity{}, mesh_entity{}, plain_mesh_entity{};
  entity_id camera_node{}, mesh_node{}, plain_mesh_node{};
  const native::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  native::mesh_component component{.mesh = mesh_owner, .material = material_owner};
  native::mesh_component plain_component{.mesh = mesh_owner, .material = plain_material_owner};
  native::math::transform camera_transform;
  camera_transform.translation[2] = 2.0F;
  native::math::transform near_transform;
  near_transform.translation[0] = -0.55F;
  near_transform.translation[2] = 0.5F;
  native::math::transform plain_transform;
  plain_transform.translation[0] = 0.55F;
  plain_transform.translation[2] = 0.5F;
  auto object_scene = world.create_scene();
  if (!object_scene)
    return 2;
  auto camera_object = object_scene->create_object({"camera", {}});
  auto mesh_object = object_scene->create_object({"mesh", {}});
  auto plain_object = object_scene->create_object({"plain", {}});
  if (!camera_object || !mesh_object || !plain_object)
    return 2;
  camera_entity = camera_object->entity();
  mesh_entity = mesh_object->entity();
  plain_mesh_entity = plain_object->entity();
  camera_node = entity_id{camera_entity.get()};
  mesh_node = entity_id{mesh_entity.get()};
  plain_mesh_node = entity_id{plain_mesh_entity.get()};
  if (camera_object->set_local_transform(camera_transform).failed() ||
      mesh_object->set_local_transform(near_transform).failed() ||
      plain_object->set_local_transform(plain_transform).failed() ||
      camera_object->add_component<gneiss::camera_component>(camera, true).failed() ||
      gneiss::set_active_camera(world, camera_entity).failed() ||
      mesh_object->add_component<gneiss::mesh_component>(component).failed() ||
      plain_object->add_component<gneiss::mesh_component>(plain_component).failed()) {
    std::fprintf(stderr, "测试场景构造失败\n");
    return 3;
  }
  auto run_result = application->step();
  if (run_result)
    run_result = application->step();
  if (!run_result) {
    std::fprintf(stderr, "Application 运行失败：%d\n", run_result.error().code());
    return 4;
  }
  // 捕获尺寸独立于窗口；连续调整窗口后仍能正常呈现，最小化不阻止离屏捕获。
  render::frame_image captured;
  const auto original = application->window()->surface();
  if (capture(64, 48, captured).failed() || captured.width != 64 || captured.height != 48)
    return 13;
  const auto current = application->window()->surface();
  if (current.width != original.width || current.height != original.height)
    return 14;
#ifdef _WIN32
  for (int width : {400, 480, 360}) {
    if (!SetWindowPos(window, nullptr, 0, 0, width, 300,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) ||
        application->step().failed() || application->step().failed() ||
        resources.finish_frames().failed())
      return 15;
  }
  ShowWindow(window, SW_MINIMIZE);
  if (application->step().failed() || application->window()->surface().width != 0 ||
      application->window()->surface().height != 0 || capture(80, 60, captured).failed() ||
      captured.width != 80 || captured.height != 60)
    return 16;
  ShowWindow(window, SW_SHOWNOACTIVATE);
  if (application->step().failed() || application->step().failed() ||
      resources.finish_frames().failed())
    return 17;
#endif
  native::rid replacement_mesh{};
  if (resources.destroy_mesh(mesh).failed() ||
      gneiss::test_support::take_value(resources_factory.create(mesh_desc), mesh_owner).failed()) {
    return 5;
  }
  replacement_mesh = resource_handle(mesh_owner);
  component.mesh = mesh_owner;
  plain_component.mesh = mesh_owner;
  *world.find(mesh_entity).try_component<gneiss::mesh_component>() = component;
  *world.find(plain_mesh_entity).try_component<gneiss::mesh_component>() = plain_component;
  if (world.find(mesh_entity).notify_component_modified<gneiss::mesh_component>().failed() ||
      world.find(plain_mesh_entity).notify_component_modified<gneiss::mesh_component>().failed())
    return 7;
  run_result = application->step();
  if (run_result.failed()) {
    std::fprintf(stderr, "Mesh generation 更新后运行失败：%d\n", run_result.error().code());
    return 7;
  }
  if (resources.destroy_mesh(replacement_mesh).failed() ||
      resources.destroy_material(material).failed() ||
      resources.destroy_material(plain_material).failed() ||
      resources.destroy_texture(texture).failed()) {
    return 8;
  }
  object_scene->unload();
  context.ui.clear();
  context.debug = {};
  binding->reset();
  owned_world->reset();
  application->request_exit();
  if (application->run().failed())
    return 12;
#ifdef _WIN32
  // 重建窗口必须分配新身份，不能再次使用单窗口占位值 1。
  auto next_config = config;
  gneiss::window::id next_id{};
  std::shared_ptr<gneiss::runtime::application> next;
  auto created = gneiss::runtime::application::create(next_config);
  if (!created)
    return 18;
  next = *created;
  next->add_system(gneiss::app::stage::update, "smoke.input", [&](const native::frame_time&) {
    input::event event;
    while (next->input().poll(event) == error::success) {
      if (event.type == input::event_type::key)
        next_id = event.window_id;
    }
    return error::success;
  });
  const auto next_window = FindWindowA(nullptr, title.data());
  if (!next_window || !PostMessageW(next_window, WM_KEYDOWN, 'A', 0x001e0001) ||
      !PostMessageW(next_window, WM_KEYUP, 'A', static_cast<LPARAM>(0xc01e0001U)) ||
      next->step().failed() || next->step().failed() || !next_id || next_id == context.window_id)
    return 19;
  next->request_exit();
  if (!next->run())
    return 19;
#endif
  return 0;
}
