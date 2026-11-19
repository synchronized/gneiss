// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/resource/texture/texture_view.hpp>

#include <array>

int main() try {
  namespace native = gneiss;
  std::shared_ptr<gneiss::runtime::application> first;
  std::shared_ptr<gneiss::runtime::application> second;
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create({}), first)
          .failed() ||
      gneiss::test_support::assign_value(gneiss::runtime::application::create({}), second).failed())
    return 1;
  auto& resources = first->render();
  auto& other = second->render();
  // 目标身份由 Render 校验；视图矩阵和重复输出由 render_scene_test 覆盖。
  auto target = resources.create_target({320, 240});
  if (!target || !resources.is_target(*target) || other.is_target(*target) ||
      !resources.destroy_target(*target))
    return 31;
  auto completion = resources.try_take_required_completion();
  if (!completion || completion->has_value())
    return 30;
  const std::array vertices{native::mesh_vertex{{-0.5F, -0.5F, 0.0F}, {0.0F, 0.0F}},
                            native::mesh_vertex{{0.5F, -0.5F, 0.0F}, {1.0F, 0.0F}},
                            native::mesh_vertex{{0.0F, 0.5F, 0.0F}, {0.5F, 1.0F}}};
  const std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
  const std::array<std::uint8_t, 4> pixels{255U, 0U, 0U, 255U};
  native::mesh_view mesh_desc;
  mesh_desc.vertices = vertices;
  mesh_desc.indices = indices;
  native::material_data material_desc;
  material_desc.parameters.color[0] = 0.25F;
  material_desc.parameters.color[1] = 0.5F;
  material_desc.parameters.color[2] = 0.75F;
  native::texture_view texture_desc;
  texture_desc.width = texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixels = pixels;
  native::rid mesh{}, material{}, texture{};
  if (gneiss::test_support::assign_value(resources.create_mesh(mesh_desc), mesh).failed() ||
      gneiss::test_support::assign_value(resources.create_material(material_desc), material)
          .failed() ||
      gneiss::test_support::assign_value(resources.create_texture(texture_desc), texture)
          .failed() ||
      mesh == 0 || material == 0 || texture == 0)
    return 2;
  if (resources.destroy_mesh(material) != native::error::invalid_handle ||
      other.destroy_mesh(mesh) != native::error::invalid_handle ||
      other.destroy_material(material) != native::error::invalid_handle ||
      resources.destroy_texture(material) != native::error::invalid_handle ||
      other.destroy_texture(texture) != native::error::invalid_handle)
    return 3;
  if (resources.destroy_mesh(mesh).failed() ||
      resources.destroy_mesh(mesh) != native::error::invalid_handle ||
      resources.destroy_material(material).failed() ||
      resources.destroy_material(material) != native::error::invalid_handle ||
      resources.destroy_texture(texture).failed() ||
      resources.destroy_texture(texture) != native::error::invalid_handle)
    return 4;
  // 非法负载不能分配资源。
  mesh = material = texture = 0;
  mesh_desc.vertices = std::span{vertices}.first(2);
  material_desc.parameters.color[3] = 2.0F;
  texture_desc.row_stride_bytes = 3U;
  if (gneiss::test_support::assign_value(resources.create_mesh(mesh_desc), mesh) !=
          native::error::invalid_argument ||
      gneiss::test_support::assign_value(resources.create_material(material_desc), material) !=
          native::error::invalid_argument ||
      gneiss::test_support::assign_value(resources.create_texture(texture_desc), texture) !=
          native::error::invalid_argument ||
      mesh != 0 || material != 0 || texture != 0)
    return 5;
  mesh_desc.vertices = vertices;
  mesh_desc.indices = std::span{indices}.first(1);
  if (gneiss::test_support::assign_value(resources.create_mesh(mesh_desc), mesh) !=
          native::error::invalid_argument ||
      mesh != 0)
    return 6;
  return first->shutdown().ok() && second->shutdown().ok() ? 0 : 7;
} catch (...) {
  return 99;
}
