// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_resource_service.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

int main() {
  gneiss::render_internal::render_resource_service resources;
  constexpr std::array vertices{
      gneiss_mesh_vertex{.x = 0.0F, .y = 0.0F, .z = 0.0F, .u = 0.0F, .v = 0.0F},
      gneiss_mesh_vertex{.x = 1.0F, .y = 0.0F, .z = 0.0F, .u = 1.0F, .v = 0.0F},
      gneiss_mesh_vertex{.x = 0.0F, .y = 1.0F, .z = 0.0F, .u = 0.0F, .v = 1.0F}};
  constexpr std::array normals{gneiss_mesh_normal{.x = 0.0F, .y = 0.0F, .z = 1.0F},
                               gneiss_mesh_normal{.x = 0.0F, .y = 0.0F, .z = 1.0F},
                               gneiss_mesh_normal{.x = 0.0F, .y = 0.0F, .z = 1.0F}};
  gneiss_mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  mesh_desc.vertex_count = static_cast<std::uint32_t>(vertices.size());
  mesh_desc.vertices = vertices.data();
  mesh_desc.normal_count = static_cast<std::uint32_t>(normals.size());
  mesh_desc.normals = normals.data();
  constexpr std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
  mesh_desc.index_count = static_cast<std::uint32_t>(indices.size());
  mesh_desc.indices = indices.data();
  gneiss_mesh mesh = GNEISS_NULL_MESH;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_SUCCESS ||
      resources.get_mesh(mesh)->normals.size() != normals.size() ||
      !std::ranges::equal(resources.get_mesh(mesh)->indices, indices) ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS) {
    return 10;
  }
  auto invalid_normals = normals;
  invalid_normals[0].z = 2.0F;
  mesh_desc.normal_count = static_cast<std::uint32_t>(invalid_normals.size());
  mesh_desc.normals = invalid_normals.data();
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 11;
  }
  mesh_desc.struct_size = sizeof(gneiss_mesh_desc) - 1U;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 12;
  }
  mesh_desc.struct_size = sizeof(gneiss_mesh_desc);
  mesh_desc.normals = normals.data();
  mesh_desc.index_count = 2U;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 13;
  }
  mesh_desc.index_count = static_cast<std::uint32_t>(indices.size());
  constexpr std::array<std::uint32_t, 3> invalid_indices{0U, 1U, 3U};
  mesh_desc.indices = invalid_indices.data();
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 14;
  }
  mesh_desc.indices = indices.data();
  mesh_desc.reserved_3 = 1U;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 15;
  }
  const std::array<std::uint8_t, 20> source{1,  2,  3, 4,  5,  6,  7,  8,  90, 91,
                                            92, 93, 9, 10, 11, 12, 13, 14, 15, 16};
  gneiss_texture_desc desc = GNEISS_TEXTURE_DESC_INIT;
  desc.width = 2;
  desc.height = 2;
  desc.row_stride_bytes = 12;
  desc.pixel_data_size = source.size();
  desc.pixels = source.data();

  gneiss_texture texture = GNEISS_NULL_TEXTURE;
  if (resources.create_texture(desc, &texture) != GNEISS_SUCCESS ||
      texture == GNEISS_NULL_TEXTURE || resources.live_resource_count() != 1U) {
    return 1;
  }
  const auto* stored = resources.get_texture(texture);
  const std::array<std::byte, 16> expected{
      std::byte{1},  std::byte{2},  std::byte{3},  std::byte{4},  std::byte{5},  std::byte{6},
      std::byte{7},  std::byte{8},  std::byte{9},  std::byte{10}, std::byte{11}, std::byte{12},
      std::byte{13}, std::byte{14}, std::byte{15}, std::byte{16}};
  if (stored == nullptr || stored->width != 2U || stored->height != 2U ||
      stored->format != GNEISS_TEXTURE_FORMAT_RGBA8_UNORM ||
      stored->color_space != GNEISS_TEXTURE_COLOR_SPACE_SRGB ||
      !std::ranges::equal(stored->pixels, expected)) {
    return 2;
  }
  if (resources.destroy_texture(texture) != GNEISS_SUCCESS ||
      resources.destroy_texture(texture) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.get_texture(texture) != nullptr || resources.live_resource_count() != 0U) {
    return 3;
  }

  desc.pixel_data_size = 19;
  texture = GNEISS_NULL_TEXTURE;
  if (resources.create_texture(desc, &texture) != GNEISS_ERROR_INVALID_ARGUMENT ||
      texture != GNEISS_NULL_TEXTURE) {
    return 4;
  }
  return 0;
}
