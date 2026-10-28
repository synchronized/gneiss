// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/resource/texture/texture_view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
bool verify_borrowed_inputs() {
  using namespace gneiss::engine;
  render_service resources;
  std::array<gneiss::engine::mesh_vertex, 3> vertices{};
  gneiss::engine::mesh_view mesh_source;
  mesh_source.vertices = vertices;
  gneiss::engine::rid mesh{};
  if (resources.create_mesh(mesh_source, &mesh) != gneiss::engine::result::success) {
    return false;
  }
  vertices[0].position.x = 42.0F;
  auto retained_mesh = resources.share_mesh(mesh);
  if (retained_mesh->vertices[0].position.x != 0.0F ||
      resources.destroy_mesh(mesh) != gneiss::engine::result::success ||
      retained_mesh->vertices.size() != 3U) {
    return false;
  }
  // 拥有型提交转交原数组；校验与借用路径一致。
  gneiss::engine::mesh_data owned;
  owned.vertices.resize(3);
  const auto* storage = owned.vertices.data();
  if (resources.create_mesh(std::move(owned), &mesh) != gneiss::engine::result::success ||
      resources.get_mesh(mesh)->vertices.data() != storage ||
      resources.destroy_mesh(mesh) != gneiss::engine::result::success)
    return false;
  owned.vertices.resize(3);
  owned.vertices[0].position.x = std::numeric_limits<float>::infinity();
  if (resources.create_mesh(std::move(owned), &mesh) != gneiss::engine::result::invalid_argument)
    return false;
  std::array<std::uint8_t, 4> pixels{1U, 2U, 3U, 4U};
  texture_view texture_source;
  texture_source.width = 1U;
  texture_source.height = 1U;
  texture_source.row_stride_bytes = 4U;
  texture_source.pixels = pixels;
  gneiss::engine::rid texture{};
  if (resources.create_texture(texture_source, &texture) != gneiss::engine::result::success) {
    return false;
  }
  pixels[0] = 9U;
  auto retained_texture = resources.share_texture(texture);
  if (retained_texture->levels[0].pixels[0] != std::byte{1} ||
      resources.destroy_texture(texture) != gneiss::engine::result::success ||
      retained_texture->levels[0].pixels.size() != 4U) {
    return false;
  }
  const std::array<math::vector3, 1> invalid_normals{};
  mesh_source.normals = invalid_normals;
  return resources.create_mesh(mesh_source, &mesh) == result::invalid_argument;
}

bool verify_shared_budget() {
  using namespace gneiss::engine;
  render_service resources{32U};
  const std::array<std::uint8_t, 16> pixels{};
  gneiss::engine::texture_view desc;
  desc.width = 2U;
  desc.height = 2U;
  desc.row_stride_bytes = 8U;
  desc.pixels = pixels;
  gneiss::engine::rid active{}, candidate{}, rejected{};
  if (resources.create_texture(desc, &active) != gneiss::engine::result::success ||
      resources.create_texture(desc, &candidate) != gneiss::engine::result::success) {
    return false;
  }
  auto frame = resources.share_texture(active);
  auto prepared = resources.share_texture(candidate);
  // 发布相同对象不重复收费；旧帧在 RID 销毁后仍占额度。
  if (!resources.replace_texture(candidate, prepared) ||
      resources.memory_usage().logical_bytes != 32U ||
      resources.memory_usage().cpu_data_bytes != 32U ||
      resources.destroy_texture(active) != gneiss::engine::result::success ||
      resources.available_memory_bytes() != 0U) {
    return false;
  }
  rejected = 123U;
  if (resources.create_texture(desc, &rejected) != gneiss::engine::result::out_of_memory ||
      rejected != 0U || resources.share_texture(candidate) != prepared) {
    return false;
  }
  const auto replacement = std::make_shared<const texture_data>(*prepared);
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
         resources.create_texture(desc, &rejected) == gneiss::engine::result::success &&
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
  gneiss::engine::render_service resources;
  constexpr std::array vertices{
      gneiss::engine::mesh_vertex{.position = {0.0F, 0.0F, 0.0F}, .uv = {0.0F, 0.0F}},
      gneiss::engine::mesh_vertex{.position = {1.0F, 0.0F, 0.0F}, .uv = {1.0F, 0.0F}},
      gneiss::engine::mesh_vertex{.position = {0.0F, 1.0F, 0.0F}, .uv = {0.0F, 1.0F}}};
  constexpr std::array normals{gneiss::engine::math::vector3{0.0F, 0.0F, 1.0F},
                               gneiss::engine::math::vector3{0.0F, 0.0F, 1.0F},
                               gneiss::engine::math::vector3{0.0F, 0.0F, 1.0F}};
  gneiss::engine::mesh_view mesh_desc;
  mesh_desc.vertices = vertices;
  mesh_desc.normals = normals;
  constexpr std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
  mesh_desc.indices = indices;
  gneiss::engine::rid mesh = 0;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::success ||
      resources.get_mesh(mesh)->normals.size() != normals.size() ||
      !std::ranges::equal(resources.get_mesh(mesh)->indices, indices) ||
      resources.destroy_mesh(mesh) != gneiss::engine::result::success) {
    return 10;
  }
  std::array tangents{gneiss::engine::math::vector4{1, 0, 0, 1},
                      gneiss::engine::math::vector4{1, 0, 0, -1},
                      gneiss::engine::math::vector4{1, 0, 0, 1}};
  mesh_desc.tangents = tangents;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::success ||
      resources.get_mesh(mesh)->tangents[1].w != -1.0F ||
      resources.destroy_mesh(mesh) != gneiss::engine::result::success)
    return 18;
  tangents[0].w = 0.0F;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument)
    return 19;
  tangents[0] = {0, 0, 1, 1};
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument)
    return 21;
  mesh_desc.tangents = {};
  auto tiled_vertices = vertices;
  tiled_vertices[0].uv.x = -42469.96875F;
  tiled_vertices[1].uv.y = 22.5F;
  mesh_desc.vertices = tiled_vertices;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::success ||
      resources.get_mesh(mesh)->vertices[0].uv.x != tiled_vertices[0].uv.x ||
      resources.get_mesh(mesh)->vertices[1].uv.y != tiled_vertices[1].uv.y ||
      resources.destroy_mesh(mesh) != gneiss::engine::result::success) {
    return 16;
  }
  for (const auto invalid :
       {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    tiled_vertices[0].uv.x = invalid;
    if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument ||
        resources.live_resource_count() != 0U) {
      return 17;
    }
    tiled_vertices[0].uv.x = 0.0F;
    tiled_vertices[0].uv.y = invalid;
    if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument ||
        resources.live_resource_count() != 0U) {
      return 18;
    }
    tiled_vertices[0].uv.y = 0.0F;
  }
  mesh_desc.vertices = vertices;
  auto invalid_normals = normals;
  invalid_normals[0].z = 2.0F;
  mesh_desc.normals = invalid_normals;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument) {
    return 11;
  }
  mesh_desc.normals = normals;
  mesh_desc.indices = std::span{indices}.first(2);
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument) {
    return 13;
  }
  constexpr std::array<std::uint32_t, 3> invalid_indices{0U, 1U, 3U};
  mesh_desc.indices = invalid_indices;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument) {
    return 14;
  }
  mesh_desc.indices = indices;
  std::array<gneiss::engine::math::vector2, 3> uv1{{{0, 1}, {1, 0}, {0.5F, 0.5F}}};
  std::array<gneiss::engine::math::vector4, 3> colors{
      {{1, 0, 0, 1}, {0, 1, 0, 0.5F}, {0, 0, 1, 0}}};
  mesh_desc.uv1 = uv1;
  mesh_desc.colors = colors;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::success ||
      resources.get_mesh(mesh)->uv1.size() != 3U || resources.get_mesh(mesh)->colors[1].a != 0.5F ||
      resources.destroy_mesh(mesh) != gneiss::engine::result::success)
    return 19;
  mesh_desc.uv1 = std::span{uv1}.first(2);
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument)
    return 20;
  mesh_desc.uv1 = uv1;
  colors[0].r = -1.0F;
  if (resources.create_mesh(mesh_desc, &mesh) != gneiss::engine::result::invalid_argument)
    return 21;
  const std::array<std::uint8_t, 20> source{1,  2,  3, 4,  5,  6,  7,  8,  90, 91,
                                            92, 93, 9, 10, 11, 12, 13, 14, 15, 16};
  gneiss::engine::texture_view desc;
  desc.width = 2;
  desc.height = 2;
  desc.row_stride_bytes = 12;
  desc.pixels = source;

  gneiss::engine::rid texture = 0;
  if (resources.create_texture(desc, &texture) != gneiss::engine::result::success || texture == 0 ||
      resources.live_resource_count() != 1U) {
    return 1;
  }
  const auto* stored = resources.get_texture(texture);
  const std::array<std::byte, 16> expected{
      std::byte{1},  std::byte{2},  std::byte{3},  std::byte{4},  std::byte{5},  std::byte{6},
      std::byte{7},  std::byte{8},  std::byte{9},  std::byte{10}, std::byte{11}, std::byte{12},
      std::byte{13}, std::byte{14}, std::byte{15}, std::byte{16}};
  if (stored == nullptr || stored->width != 2U || stored->height != 2U ||
      stored->format != gneiss::engine::texture_format::rgba8_unorm ||
      stored->color_space != gneiss::engine::texture_color_space::srgb ||
      stored->levels.size() != 1U || !std::ranges::equal(stored->levels.front().pixels, expected)) {
    return 2;
  }
  if (resources.destroy_texture(texture) != gneiss::engine::result::success ||
      resources.destroy_texture(texture) != gneiss::engine::result::invalid_handle ||
      resources.get_texture(texture) != nullptr || resources.live_resource_count() != 0U) {
    return 3;
  }

  desc.pixels = std::span{source}.first(19);
  texture = 0;
  if (resources.create_texture(desc, &texture) != gneiss::engine::result::invalid_argument ||
      texture != 0) {
    return 4;
  }
  return 0;
}
