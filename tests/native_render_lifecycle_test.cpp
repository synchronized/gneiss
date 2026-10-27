// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/texture_view.hpp"
#include "runtime/application.hpp"

#include <array>

int main() try {
  namespace native = gneiss::engine;
  std::shared_ptr<gneiss::runtime::application> first;
  std::shared_ptr<gneiss::runtime::application> second;
  if (gneiss::runtime::application::create({}, first).failed() ||
      gneiss::runtime::application::create({}, second).failed())
    return 1;
  auto& resources = first->resources();
  auto& other = second->resources();
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
  if (resources.create_mesh(mesh_desc, &mesh).failed() ||
      resources.create_material(material_desc, &material).failed() ||
      resources.create_texture(texture_desc, &texture).failed() || mesh == 0 || material == 0 ||
      texture == 0)
    return 2;
  if (resources.destroy_mesh(material) != native::result::invalid_handle ||
      other.destroy_mesh(mesh) != native::result::invalid_handle ||
      other.destroy_material(material) != native::result::invalid_handle ||
      resources.destroy_texture(material) != native::result::invalid_handle ||
      other.destroy_texture(texture) != native::result::invalid_handle)
    return 3;
  if (resources.destroy_mesh(mesh).failed() ||
      resources.destroy_mesh(mesh) != native::result::invalid_handle ||
      resources.destroy_material(material).failed() ||
      resources.destroy_material(material) != native::result::invalid_handle ||
      resources.destroy_texture(texture).failed() ||
      resources.destroy_texture(texture) != native::result::invalid_handle)
    return 4;
  // 非法负载不能分配资源。
  mesh = material = texture = 0;
  mesh_desc.vertices = std::span{vertices}.first(2);
  material_desc.parameters.color[3] = 2.0F;
  texture_desc.row_stride_bytes = 3U;
  if (resources.create_mesh(mesh_desc, &mesh) != native::result::invalid_argument ||
      resources.create_material(material_desc, &material) != native::result::invalid_argument ||
      resources.create_texture(texture_desc, &texture) != native::result::invalid_argument ||
      mesh != 0 || material != 0 || texture != 0)
    return 5;
  mesh_desc.vertices = vertices;
  mesh_desc.indices = std::span{indices}.first(1);
  if (resources.create_mesh(mesh_desc, &mesh) != native::result::invalid_argument || mesh != 0)
    return 6;
  return first->shutdown().ok() && second->shutdown().ok() ? 0 : 7;
} catch (...) {
  return 99;
}
