// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/render_resource_factory.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

#include <array>
#include <cstdio>
#include <string_view>

using namespace gneiss;
namespace native = gneiss::engine;

namespace {

struct smoke_context final {
  native::texture texture;
  bool received_key{};
};

native::result submit_ui(gneiss::runtime::application& application, smoke_context& context) {
  native::input_event event;
  while (application.poll_input(event) == native::result::success) {
    // USB HID 物理键编号：A 为 4。
    if (event.type == native::event_type::key && event.data.key.physical_key == 4U &&
        event.data.key.action == native::key_action::pressed) {
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
  auto result = application.submit_ui_draw_list(draw_list);
  constexpr std::array lines{native::debug_line{.start = {-0.8F, 0.0F, 0.0F},
                                                .end = {0.8F, 0.0F, 0.0F},
                                                .color_rgba8 = UINT32_C(0xff60a5fa),
                                                .width = 2.0F,
                                                .depth_test = true}};
  if (result == native::result::success) {
    result = application.submit_debug_draw_list(lines);
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
  config.callbacks.update = [&](const native::frame_time&) {
    return submit_ui(*application, context);
  };
  const auto create_result = gneiss::runtime::application::create(config, application);
  if (create_result.failed()) {
    std::fprintf(stderr, "Application 创建失败：%d\n", create_result.code());
    return 1;
  }
  auto& resources = application->render();
  auto& resources_factory = application->resource_factory();
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
  native::mesh mesh_owner;
  native::material material_owner, plain_material_owner;
  if (resources_factory.create_texture(texture_desc, context.texture).failed()) {
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
  if (application->run(2) != native::result::success) {
    std::fprintf(stderr, "纯 UI 帧运行失败\n");
    return 10;
  }
#ifdef _WIN32
  if (!context.received_key) {
    std::fprintf(stderr, "窗口按键消息未到达 Application\n");
    return 11;
  }
#endif
  if (resources_factory.create_mesh(mesh_desc, mesh_owner).failed() ||
      resources_factory.create_material(material_desc.parameters, {}, plain_material_owner)
          .failed()) {
    std::fprintf(stderr, "测试基础资源创建失败\n");
    return 2;
  }
  mesh = resource_handle(mesh_owner);
  plain_material = resource_handle(plain_material_owner);
  material_desc.base_color_texture = texture;
  if (resources_factory.create_material(material_desc.parameters, {context.texture}, material_owner)
          .failed()) {
    std::fprintf(stderr, "测试资源创建失败\n");
    return 2;
  }
  material = resource_handle(material_owner);
  auto& world = *application->native_world();
  entity_id camera_entity{}, mesh_entity{}, plain_mesh_entity{};
  scene_node_id camera_node{}, mesh_node{}, plain_mesh_node{};
  const native::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  native::mesh_renderer mesh_renderer{.mesh = resources_factory.find_mesh(mesh),
                                      .material = resources_factory.find_material(material)};
  native::mesh_renderer plain_mesh_renderer{.mesh = resources_factory.find_mesh(mesh),
                                            .material =
                                                resources_factory.find_material(plain_material)};
  native::math::transform camera_transform;
  camera_transform.translation[2] = 2.0F;
  native::math::transform near_transform;
  near_transform.translation[0] = -0.55F;
  near_transform.translation[2] = 0.5F;
  native::math::transform plain_transform;
  plain_transform.translation[0] = 0.55F;
  plain_transform.translation[2] = 0.5F;
  if (world.entity_create(camera_entity).failed() || world.entity_create(mesh_entity).failed() ||
      world.entity_create(plain_mesh_entity).failed() ||
      world.node_create({}, camera_entity, camera_node).failed() ||
      world.node_create({}, mesh_entity, mesh_node).failed() ||
      world.node_create({}, plain_mesh_entity, plain_mesh_node).failed() ||
      world.node_set_local_transform(camera_node, camera_transform).failed() ||
      world.node_set_local_transform(mesh_node, near_transform).failed() ||
      world.node_set_local_transform(plain_mesh_node, plain_transform).failed() ||
      world.entity_set_camera(camera_entity, camera, true).failed() ||
      world.entity_set_mesh_renderer(mesh_entity, mesh_renderer).failed() ||
      world.entity_set_mesh_renderer(plain_mesh_entity, plain_mesh_renderer).failed()) {
    std::fprintf(stderr, "测试场景构造失败\n");
    return 3;
  }
  auto run_result = application->run(2);
  if (run_result != native::result::success) {
    std::fprintf(stderr, "Application 运行失败：%d\n", run_result.code());
    return 4;
  }
  native::rid replacement_mesh{};
  if (resources.destroy_mesh(mesh).failed() ||
      resources_factory.create_mesh(mesh_desc, mesh_owner).failed()) {
    return 5;
  }
  replacement_mesh = resource_handle(mesh_owner);
  mesh_renderer.mesh = mesh_owner;
  plain_mesh_renderer.mesh = resources_factory.find_mesh(replacement_mesh);
  if (world.entity_set_mesh_renderer(mesh_entity, mesh_renderer).failed() ||
      world.entity_set_mesh_renderer(plain_mesh_entity, plain_mesh_renderer).failed()) {
    return 6;
  }
  run_result = application->run(1);
  if (run_result.failed()) {
    std::fprintf(stderr, "Mesh generation 更新后运行失败：%d\n", run_result.code());
    return 7;
  }
  if (resources.destroy_mesh(replacement_mesh).failed() ||
      resources.destroy_material(material).failed() ||
      resources.destroy_material(plain_material).failed() ||
      resources.destroy_texture(texture).failed()) {
    return 8;
  }
  return application->shutdown().failed() ? 12 : 0;
}
