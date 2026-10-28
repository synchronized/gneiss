// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include "engine/resource/render_resource_factory.hpp"

#include <array>
#include <cstdint>
#include <thread>

int main() {
  gneiss::engine::render_service resources;
  gneiss::engine::render_resource_factory resources_factory{resources};
  constexpr std::array<std::uint8_t, 4> pixels{255U, 255U, 255U, 255U};
  gneiss::engine::texture_view texture_desc;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixels = pixels;
  gneiss::engine::texture texture;
  if (gneiss::engine::create_texture(resources_factory, texture_desc, texture) !=
      gneiss::engine::result::success) {
    return 1;
  }

  std::array vertices{gneiss::engine::ui_vertex{{0.0F, 0.0F}, {0.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss::engine::ui_vertex{{1.0F, 0.0F}, {1.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss::engine::ui_vertex{{0.0F, 1.0F}, {0.0F, 1.0F}, UINT32_C(0xffffffff)}};
  std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
  std::array commands{gneiss::engine::ui_draw_command{.texture = texture,
                                                      .clip_min = {0.0F, 0.0F},
                                                      .clip_max = {640.0F, 480.0F},
                                                      .first_index = 0U,
                                                      .index_count = 3U,
                                                      .vertex_offset = 0U}};
  gneiss::engine::ui_draw_view desc;
  desc.display_width = 640.0F;
  desc.display_height = 480.0F;
  desc.vertices = vertices;
  desc.indices = indices;
  desc.commands = commands;

  gneiss::engine::ui_draw_list draw_list;
  if (draw_list.replace(desc, resources) != gneiss::engine::result::success ||
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
  if (draw_list.replace(desc, resources) != gneiss::engine::result::invalid_argument ||
      draw_list.indices()[0] != 0U) {
    return 4;
  }
  indices[0] = 0U;

  commands[0].index_count = 2U;
  if (draw_list.replace(desc, resources) != gneiss::engine::result::invalid_argument ||
      draw_list.commands()[0].index_count != 3U) {
    return 5;
  }
  commands[0].index_count = 3U;

  gneiss::engine::render_service other_resources;
  gneiss::engine::render_resource_factory other_resources_factory{other_resources};
  if (draw_list.replace(desc, other_resources) != gneiss::engine::result::invalid_handle ||
      draw_list.commands().size() != 1U) {
    return 6;
  }

  draw_list.clear();
  if (!draw_list.vertices().empty() || !draw_list.indices().empty() ||
      !draw_list.commands().empty() || draw_list.display_width() != 0.0F) {
    return 7;
  }
  gneiss::engine::texture rejected = texture;
  if (gneiss::engine::create_texture(resources_factory, {}, rejected) !=
          gneiss::engine::result::invalid_argument ||
      rejected || !texture)
    return 8;
  // UI 查询不得为裸 RID 自动建立拥有者。
  using namespace gneiss::engine;
  rid unowned{};
  if (resources.create_texture(texture_desc, &unowned).failed() ||
      resources_factory.find_texture(unowned) || resources.texture_status(unowned).failed())
    return 9;
  rid legacy_material{};
  material_data legacy;
  legacy.base_color_texture = unowned;
  if (resources.create_material(legacy, &legacy_material).failed() ||
      resources_factory.reference_material(legacy_material) ||
      resources_factory.find_texture(unowned))
    return 17;
  auto adopted_texture = resources_factory.reference_texture(unowned);
  auto adopted_material = resources_factory.reference_material(legacy_material);
  if (!adopted_material || adopted_material->textures()[0] != adopted_texture)
    return 18;
  adopted_material.reset();
  adopted_texture.reset();
  (void)resources_factory.drain();

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

  rid material_handle{};
  if (resources.reserve_material(&material_handle).failed())
    return 11;
  auto material_ref = reference_material(resources_factory, material_handle);
  auto content = std::make_shared<material_data>();
  content->base_color_texture = handle;
  // 缺失依赖必须在 Render 内容变化之前拒绝。
  if (resources_factory.update_material(material_ref, content, {}) ||
      resources.material_status(material_handle) != result::not_ready ||
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
      resources_factory.create_texture(texture_desc, rejected) != result::invalid_state)
    return 14;
  // 工厂关闭不关闭服务，裸 RID 接口仍然可以使用。
  if (resources.create_texture(texture_desc, &unowned).failed())
    return 15;
  (void)resources.destroy_texture(unowned);
  std::thread worker([value = std::move(material_ref)]() mutable { value.reset(); });
  worker.join();
  if (resources_factory.drain() != 0)
    return 16;
  return 0;
}
