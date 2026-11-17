// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include "function/asset/detail/asset_construction.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/render/ui_draw_list.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <thread>
#include <type_traits>

template <typename Factory>
concept can_adopt_texture =
    requires(Factory& factory, gneiss::rid handle) { factory.reference_texture(handle); };
static_assert(!can_adopt_texture<gneiss::detail::asset_construction>);
static_assert(!std::is_constructible_v<gneiss::texture, gneiss::rid>);

int main() {
  gneiss::render::service resources;
  gneiss::detail::asset_construction resources_factory{resources};
  constexpr std::array<std::uint8_t, 4> pixels{255U, 255U, 255U, 255U};
  gneiss::texture_view texture_desc;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixels = pixels;
  gneiss::ref<const gneiss::texture> texture;
  if (gneiss::test_support::take_value(resources_factory.create_texture(texture_desc), texture) !=
      gneiss::error::success) {
    return 1;
  }

  std::array vertices{gneiss::ui_vertex{{0.0F, 0.0F}, {0.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss::ui_vertex{{1.0F, 0.0F}, {1.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss::ui_vertex{{0.0F, 1.0F}, {0.0F, 1.0F}, UINT32_C(0xffffffff)}};
  std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
  std::array commands{gneiss::ui_draw_command{.texture = texture,
                                              .clip_min = {0.0F, 0.0F},
                                              .clip_max = {640.0F, 480.0F},
                                              .first_index = 0U,
                                              .index_count = 3U,
                                              .vertex_offset = 0U}};
  gneiss::ui_draw_view desc;
  desc.display_width = 640.0F;
  desc.display_height = 480.0F;
  desc.vertices = vertices;
  desc.indices = indices;
  desc.commands = commands;

  gneiss::ui_draw_list draw_list;
  if (draw_list.replace(desc, resources) != gneiss::error::success ||
      draw_list.vertices().size() != 3U || draw_list.indices().size() != 3U ||
      draw_list.commands().size() != 1U) {
    return 2;
  }
  vertices[0].position[0] = 9.0F;
  indices[0] = 2U;
  commands[0].index_count = 0U;
  if (draw_list.vertices()[0].position[0] != 0.0F || draw_list.indices()[0] != 0U ||
      draw_list.commands()[0].index_count != 3U) {
    return 3;
  }

  indices[0] = 3U;
  commands[0].index_count = 3U;
  if (draw_list.replace(desc, resources) != gneiss::error::invalid_argument ||
      draw_list.indices()[0] != 0U) {
    return 4;
  }
  indices[0] = 0U;

  commands[0].index_count = 2U;
  if (draw_list.replace(desc, resources) != gneiss::error::invalid_argument ||
      draw_list.commands()[0].index_count != 3U) {
    return 5;
  }
  commands[0].index_count = 3U;

  gneiss::render::service other_resources;
  gneiss::detail::asset_construction other_resources_factory{other_resources};
  if (draw_list.replace(desc, other_resources) != gneiss::error::invalid_handle ||
      draw_list.commands().size() != 1U) {
    return 6;
  }

  const auto fail_target = [](const char* message) {
    std::cerr << "UI target: " << message << "\n";
    return 1;
  };
  // 目标图像必须来自同服务，提交时必须有同帧生产视图和窗口消费者。
  auto target = resources.create_target({64, 64});
  auto scene = resources.create_scene();
  if (!target || !scene)
    return fail_target("create target and scene");
  commands[0].texture = gneiss::ui_target_image{*target};
  if (draw_list.replace(desc, resources).failed() ||
      draw_list.replace(desc, other_resources) != gneiss::error::invalid_handle)
    return fail_target("accept local target, reject foreign service");
  gneiss::render::frame_request request{
      .surface = {.width = 64, .height = 64}, .views = {{.scene = *scene}}, .ui = draw_list};
  auto missing_producer = resources.render(std::move(request));
  if (missing_producer || missing_producer.error() != gneiss::error::invalid_argument ||
      request.ui.commands().size() != 1)
    return fail_target("reject missing producer without consuming request");
  request.views.push_back({.scene = *scene, .target = *target});
  auto ready = resources.render(std::move(request));
  if (ready || ready.error() != gneiss::error::not_ready)
    return fail_target("valid request reaches executor readiness check");
  auto image = std::make_shared<gneiss::render::frame_image>();
  auto capture = resources.request_capture(std::move(request), image);
  if (capture || capture.error() != gneiss::error::not_ready)
    return fail_target("window composite capture reaches executor readiness check");
  if (!resources.destroy_target(*target))
    return fail_target("destroy target");
  auto stale = resources.render(std::move(request));
  if (stale || stale.error() != gneiss::error::invalid_handle ||
      draw_list.replace(desc, resources) != gneiss::error::invalid_handle)
    return fail_target("reject retired target");
  if (!resources.destroy_scene(*scene))
    return fail_target("destroy scene");
  commands[0].texture = texture;
  draw_list.clear();
  if (!draw_list.vertices().empty() || !draw_list.indices().empty() ||
      !draw_list.commands().empty() || draw_list.display_width() != 0.0F) {
    return 7;
  }
  gneiss::ref<const gneiss::texture> rejected = texture;
  if (gneiss::test_support::take_value(resources_factory.create_texture({}), rejected) !=
          gneiss::error::invalid_argument ||
      rejected || !texture)
    return 8;
  // UI 查询不得为裸 RID 自动建立拥有者。
  using namespace gneiss;
  rid unowned{};
  if (gneiss::test_support::assign_value(resources.create_texture(texture_desc), unowned)
          .failed() ||
      resources_factory.find_texture(unowned) || resources.texture_status(unowned).failed())
    return 9;
  // 两个工厂共用一个 Render，也不能从裸 RID 建立第二个拥有者。
  detail::asset_construction peer{resources};
  if (peer.find_texture(resource_handle(texture)))
    return 17;
  gneiss::ref<const gneiss::texture> peer_texture;
  if (gneiss::test_support::take_value(peer.create_texture(texture_desc), peer_texture).failed() ||
      resource_handle(peer_texture) == resource_handle(texture))
    return 18;
  const auto peer_handle = resource_handle(peer_texture);
  auto copied = peer_texture;
  peer_texture.reset();
  if (peer.drain() != 0 || peer.find_texture(peer_handle) != copied)
    return 20;
  copied.reset();
  if (peer.find_texture(peer_handle) || peer.drain() != 1 || peer.drain() != 0 ||
      resources.texture_status(peer_handle) != error::invalid_handle)
    return 21;
  gneiss::ref<const gneiss::material> foreign_material;
  if (gneiss::test_support::take_value(peer.create_material({}, {texture}), foreign_material) !=
          error::invalid_argument ||
      foreign_material)
    return 22;
  if (gneiss::test_support::take_value(peer.create_texture(texture_desc), peer_texture).failed())
    return 23;
  material_parameters parameters;
  parameters.roughness = 0.25F;
  auto local_material = peer.create_material(parameters, {peer_texture});
  if (!local_material || (*local_material)->textures()[0] != peer_texture ||
      (*local_material)->parameters().roughness != parameters.roughness)
    return 24;
  const auto material_content = resources.share_material(resource_handle(*local_material));
  if (!material_content ||
      material_content->texture_handles()[0] != resource_handle(peer_texture) ||
      material_content->parameters.roughness != parameters.roughness)
    return 25;
  parameters.roughness = -1.0F;
  auto invalid_material = peer.create_material(parameters, {peer_texture});
  if (invalid_material || invalid_material.error() != error::invalid_argument ||
      resources.share_material(resource_handle(*local_material)) != material_content)
    return 26;
  peer.close();
  if (resources.texture_status(resource_handle(texture)).failed() ||
      resources.texture_status(resource_handle(peer_texture)) != error::invalid_handle)
    return 19;
  (void)resources.destroy_texture(unowned);

  const auto handle = resource_handle(texture);
  auto changed = std::make_shared<texture_data>(*resources.share_texture(handle));
  const auto original_color_space = texture->metadata()->color_space;
  changed->color_space = original_color_space == texture_color_space::linear
                             ? texture_color_space::srgb
                             : texture_color_space::linear;
  // 直接 Render 更新只影响渲染端；上层元信息由工厂驱动更新。
  if (!resources.replace_texture(handle, changed) ||
      texture->metadata()->color_space != original_color_space ||
      !resources_factory.update_texture(texture, changed) ||
      texture->metadata()->color_space != changed->color_space ||
      other_resources_factory.update_texture(texture, changed))
    return 10;

  gneiss::ref<const gneiss::material> material_ref;
  if (gneiss::test_support::take_value(resources_factory.reserve_material(), material_ref).failed())
    return 11;
  const auto material_handle = resource_handle(material_ref);
  auto content = std::make_shared<material_data>();
  content->base_color_texture = handle;
  // 缺失依赖必须在 Render 内容变化之前拒绝。
  if (resources_factory.update_material(material_ref, content, {}) ||
      resources.material_status(material_handle) != error::not_ready ||
      !resources_factory.update_material(material_ref, content, {texture}) ||
      material_ref->textures()[0] != texture)
    return 12;
  auto wrong = std::make_shared<material_data>(*content);
  wrong->base_color_texture = null_rid;
  if (resources_factory.update_material(material_ref, wrong, {texture}) ||
      resources.get_material(material_handle)->base_color_texture != handle ||
      material_ref->textures()[0] != texture)
    return 13;

  resources_factory.close();
  resources_factory.close();
  if (!resources.is_valid() || resources.live_resource_count() != 0 ||
      resources_factory.find_texture(handle) ||
      resources_factory.update_texture(texture, changed) ||
      gneiss::test_support::take_value(resources_factory.create_texture(texture_desc), rejected) !=
          error::invalid_state)
    return 14;
  // 工厂关闭不关闭服务，裸 RID 接口仍然可以使用。
  if (gneiss::test_support::assign_value(resources.create_texture(texture_desc), unowned).failed())
    return 15;
  (void)resources.destroy_texture(unowned);
  std::thread worker([value = std::move(material_ref)]() mutable { value.reset(); });
  worker.join();
  if (resources_factory.drain() != 0)
    return 16;
  return 0;
}
