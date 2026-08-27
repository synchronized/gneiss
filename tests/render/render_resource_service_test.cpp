// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_resource_service.h"

#include <algorithm>
#include <array>
#include <cstddef>

int main() {
  gneiss::render_internal::render_resource_service resources;
  constexpr std::array vertices{gneiss_mesh_vertex{0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                                gneiss_mesh_vertex{1.0F, 0.0F, 0.0F, 1.0F, 0.0F},
                                gneiss_mesh_vertex{0.0F, 1.0F, 0.0F, 0.0F, 1.0F}};
  constexpr std::array normals{gneiss_mesh_normal{0.0F, 0.0F, 1.0F},
                               gneiss_mesh_normal{0.0F, 0.0F, 1.0F},
                               gneiss_mesh_normal{0.0F, 0.0F, 1.0F}};
  gneiss_mesh_desc mesh_desc = GNEISS_MESH_DESC_INIT;
  mesh_desc.vertex_count = vertices.size();
  mesh_desc.vertices = vertices.data();
  mesh_desc.normal_count = normals.size();
  mesh_desc.normals = normals.data();
  gneiss_mesh mesh = GNEISS_NULL_MESH;
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_SUCCESS ||
      resources.get_mesh(mesh)->normals.size() != normals.size() ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS) {
    return 10;
  }
  auto invalid_normals = normals;
  invalid_normals[0].z = 2.0F;
  mesh_desc.struct_size = GNEISS_MESH_DESC_VERSION_1_SIZE;
  mesh_desc.reserved_2 = 99U;
  mesh_desc.normal_count = invalid_normals.size();
  mesh_desc.normals = invalid_normals.data();
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_SUCCESS ||
      !resources.get_mesh(mesh)->normals.empty() ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS) {
    return 11;
  }
  mesh_desc.struct_size = sizeof(gneiss_mesh_desc);
  mesh_desc.reserved_2 = 0U;
  mesh_desc.normals = invalid_normals.data();
  if (resources.create_mesh(mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 12;
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
