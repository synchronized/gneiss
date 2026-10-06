// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/mesh_description.hpp"
#include "engine/api/texture_description.hpp"
#include "engine/function/render/render_resource_service.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
// ABI 版本兼容和服务校验合并验证，内部调用本身只使用借用视图。
gneiss_result create_mesh(gneiss::render_internal::render_resource_service& resources,
                          const gneiss_mesh_desc& desc, gneiss_mesh* output) {
  gneiss::asset_internal::mesh_view value;
  const auto converted = gneiss::api::read_mesh_description(desc, value);
  return converted == GNEISS_SUCCESS ? resources.create_mesh(value, output) : converted;
}
gneiss_result create_texture(gneiss::render_internal::render_resource_service& resources,
                             const gneiss_texture_desc& desc, gneiss_texture* output) {
  gneiss::render_internal::texture_view value;
  const auto converted = gneiss::api::read_texture_description(desc, value);
  return converted == GNEISS_SUCCESS ? resources.create_texture(value, output) : converted;
}
bool verify_borrowed_inputs() {
  using namespace gneiss::render_internal;
  render_resource_service resources;
  std::array<gneiss_mesh_vertex, 3> vertices{};
  gneiss::asset_internal::mesh_view mesh_source;
  mesh_source.vertices = vertices;
  gneiss_mesh mesh{};
  if (resources.create_mesh(mesh_source, &mesh) != GNEISS_SUCCESS) {
    return false;
  }
  vertices[0].x = 42.0F;
  auto retained_mesh = resources.share_mesh(mesh);
  if (retained_mesh->vertices[0].x != 0.0F || resources.destroy_mesh(mesh) != GNEISS_SUCCESS ||
      retained_mesh->vertices.size() != 3U) {
    return false;
  }
  std::array<std::uint8_t, 4> pixels{1U, 2U, 3U, 4U};
  texture_view texture_source;
  texture_source.width = 1U;
  texture_source.height = 1U;
  texture_source.row_stride_bytes = 4U;
  texture_source.pixels = pixels;
  gneiss_texture texture{};
  if (resources.create_texture(texture_source, &texture) != GNEISS_SUCCESS) {
    return false;
  }
  pixels[0] = 9U;
  auto retained_texture = resources.share_texture(texture);
  if (retained_texture->levels[0].pixels[0] != std::byte{1} ||
      resources.destroy_texture(texture) != GNEISS_SUCCESS ||
      retained_texture->levels[0].pixels.size() != 4U) {
    return false;
  }
  gneiss_mesh_desc invalid_mesh = GNEISS_MESH_DESC_INIT;
  invalid_mesh.vertices = vertices.data();
  invalid_mesh.vertex_count = 3U;
  invalid_mesh.normal_count = 1U;
  gneiss_texture_desc invalid_texture = GNEISS_TEXTURE_DESC_INIT;
  invalid_texture.pixels = pixels.data();
  invalid_texture.reserved[0] = 1U;
  return gneiss::api::read_mesh_description(invalid_mesh, mesh_source) ==
             GNEISS_ERROR_INVALID_ARGUMENT &&
         mesh_source.vertices.data() == vertices.data() && mesh_source.vertices.size() == 3U &&
         gneiss::api::read_texture_description(invalid_texture, texture_source) ==
             GNEISS_ERROR_INVALID_ARGUMENT &&
         texture_source.width == 1U && texture_source.pixels.data() == pixels.data();
}

bool verify_shared_budget() {
  using namespace gneiss::render_internal;
  render_resource_service resources{32U};
  const std::array<std::uint8_t, 16> pixels{};
  gneiss_texture_desc desc = GNEISS_TEXTURE_DESC_INIT;
  desc.width = 2U;
  desc.height = 2U;
  desc.row_stride_bytes = 8U;
  desc.pixels = pixels.data();
  desc.pixel_data_size = pixels.size();
  gneiss_texture active{}, candidate{}, rejected{};
  if (create_texture(resources, desc, &active) != GNEISS_SUCCESS ||
      create_texture(resources, desc, &candidate) != GNEISS_SUCCESS) {
    return false;
  }
  auto frame = resources.share_texture(active);
  auto prepared = resources.share_texture(candidate);
  // 发布相同对象不重复收费；旧帧在 RID 销毁后仍占额度。
  if (!resources.replace_texture(candidate, prepared) ||
      resources.memory_usage().logical_bytes != 32U ||
      resources.memory_usage().cpu_data_bytes != 32U ||
      resources.destroy_texture(active) != GNEISS_SUCCESS ||
      resources.available_memory_bytes() != 0U) {
    return false;
  }
  rejected = 123U;
  if (create_texture(resources, desc, &rejected) != GNEISS_ERROR_OUT_OF_MEMORY || rejected != 0U ||
      resources.share_texture(candidate) != prepared) {
    return false;
  }
  const auto replacement = std::make_shared<const texture_resource>(*prepared);
  if (resources.replace_texture(candidate, replacement) ||
      resources.share_texture(candidate) != prepared) {
    return false;
  }
  frame.reset();
  if (resources.available_memory_bytes() != 16U ||
      !resources.replace_texture(candidate, replacement) ||
      resources.memory_usage().logical_bytes != 32U) {
    return false;
  }
  // 被替换对象仍被 prepared 持有，必须继续计费。
  prepared.reset();
  return resources.available_memory_bytes() == 16U &&
         create_texture(resources, desc, &rejected) == GNEISS_SUCCESS &&
         resources.share_texture(candidate) == replacement;
}
} // namespace

int main() {
  if (!verify_borrowed_inputs()) {
    return 31;
  }
  if (!verify_shared_budget()) {
    return 30;
  }
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
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_SUCCESS ||
      resources.get_mesh(mesh)->normals.size() != normals.size() ||
      !std::ranges::equal(resources.get_mesh(mesh)->indices, indices) ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS) {
    return 10;
  }
  std::array tangents{gneiss_mesh_tangent{1, 0, 0, 1}, gneiss_mesh_tangent{1, 0, 0, -1},
                      gneiss_mesh_tangent{1, 0, 0, 1}};
  mesh_desc.tangent_count = static_cast<std::uint32_t>(tangents.size());
  mesh_desc.tangents = tangents.data();
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_SUCCESS ||
      resources.get_mesh(mesh)->tangents[1].w != -1.0F ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS)
    return 18;
  tangents[0].w = 0.0F;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT)
    return 19;
  mesh_desc.struct_size = GNEISS_MESH_DESC_VERSION_1_SIZE;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_SUCCESS ||
      !resources.get_mesh(mesh)->tangents.empty() || resources.destroy_mesh(mesh) != GNEISS_SUCCESS)
    return 20;
  mesh_desc.struct_size = sizeof(gneiss_mesh_desc);
  tangents[0] = {0, 0, 1, 1};
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT)
    return 21;
  mesh_desc.tangent_count = 0U;
  mesh_desc.tangents = nullptr;
  auto tiled_vertices = vertices;
  tiled_vertices[0].u = -42469.96875F;
  tiled_vertices[1].v = 22.5F;
  mesh_desc.vertices = tiled_vertices.data();
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_SUCCESS ||
      resources.get_mesh(mesh)->vertices[0].u != tiled_vertices[0].u ||
      resources.get_mesh(mesh)->vertices[1].v != tiled_vertices[1].v ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS) {
    return 16;
  }
  for (const auto invalid :
       {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    tiled_vertices[0].u = invalid;
    if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT ||
        resources.live_resource_count() != 0U) {
      return 17;
    }
    tiled_vertices[0].u = 0.0F;
    tiled_vertices[0].v = invalid;
    if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT ||
        resources.live_resource_count() != 0U) {
      return 18;
    }
    tiled_vertices[0].v = 0.0F;
  }
  mesh_desc.vertices = vertices.data();
  auto invalid_normals = normals;
  invalid_normals[0].z = 2.0F;
  mesh_desc.normal_count = static_cast<std::uint32_t>(invalid_normals.size());
  mesh_desc.normals = invalid_normals.data();
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 11;
  }
  mesh_desc.struct_size = sizeof(gneiss_mesh_desc) - 1U;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 12;
  }
  mesh_desc.struct_size = sizeof(gneiss_mesh_desc);
  mesh_desc.normals = normals.data();
  mesh_desc.index_count = 2U;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 13;
  }
  mesh_desc.index_count = static_cast<std::uint32_t>(indices.size());
  constexpr std::array<std::uint32_t, 3> invalid_indices{0U, 1U, 3U};
  mesh_desc.indices = invalid_indices.data();
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 14;
  }
  mesh_desc.indices = indices.data();
  mesh_desc.reserved_3 = 1U;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 15;
  }
  mesh_desc.reserved_3 = 0U;
  std::array<gneiss_mesh_uv, 3> uv1{{{0, 1}, {1, 0}, {0.5F, 0.5F}}};
  std::array<gneiss_mesh_color, 3> colors{{{1, 0, 0, 1}, {0, 1, 0, 0.5F}, {0, 0, 1, 0}}};
  mesh_desc.uv1_count = 3U;
  mesh_desc.uv1 = uv1.data();
  mesh_desc.color_count = 3U;
  mesh_desc.colors = colors.data();
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_SUCCESS ||
      resources.get_mesh(mesh)->uv1.size() != 3U || resources.get_mesh(mesh)->colors[1].a != 0.5F ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS)
    return 19;
  mesh_desc.uv1_count = 2U;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT)
    return 20;
  mesh_desc.uv1_count = 3U;
  colors[0].r = -1.0F;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_ERROR_INVALID_ARGUMENT)
    return 21;
  mesh_desc.struct_size = GNEISS_MESH_DESC_VERSION_2_SIZE;
  if (create_mesh(resources, mesh_desc, &mesh) != GNEISS_SUCCESS ||
      !resources.get_mesh(mesh)->uv1.empty() || !resources.get_mesh(mesh)->colors.empty() ||
      resources.destroy_mesh(mesh) != GNEISS_SUCCESS)
    return 22;
  const std::array<std::uint8_t, 20> source{1,  2,  3, 4,  5,  6,  7,  8,  90, 91,
                                            92, 93, 9, 10, 11, 12, 13, 14, 15, 16};
  gneiss_texture_desc desc = GNEISS_TEXTURE_DESC_INIT;
  desc.width = 2;
  desc.height = 2;
  desc.row_stride_bytes = 12;
  desc.pixel_data_size = source.size();
  desc.pixels = source.data();

  gneiss_texture texture = GNEISS_NULL_TEXTURE;
  if (create_texture(resources, desc, &texture) != GNEISS_SUCCESS ||
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
      stored->color_space != GNEISS_TEXTURE_COLOR_SPACE_SRGB || stored->levels.size() != 1U ||
      !std::ranges::equal(stored->levels.front().pixels, expected)) {
    return 2;
  }
  if (resources.destroy_texture(texture) != GNEISS_SUCCESS ||
      resources.destroy_texture(texture) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.get_texture(texture) != nullptr || resources.live_resource_count() != 0U) {
    return 3;
  }

  desc.pixel_data_size = 19;
  texture = GNEISS_NULL_TEXTURE;
  if (create_texture(resources, desc, &texture) != GNEISS_ERROR_INVALID_ARGUMENT ||
      texture != GNEISS_NULL_TEXTURE) {
    return 4;
  }
  return 0;
}
