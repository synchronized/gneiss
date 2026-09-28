// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_resource_service.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

// 精确保留扩展前布局，检查旧调用方的描述不会读取到边界外。
struct legacy_material_desc {
  std::uint32_t struct_size;
  std::uint32_t reserved;
  float red;
  float green;
  float blue;
  float alpha;
  gneiss_texture base_color_texture;
  float metallic;
  float roughness;
};

} // namespace

int main() {
  using gneiss::render_internal::render_resource_service;
  render_resource_service resources;
  render_resource_service other;
  constexpr std::array<std::uint8_t, 4> pixel{128U, 128U, 255U, 255U};
  gneiss_texture_desc texture_desc = GNEISS_TEXTURE_DESC_INIT;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixel_data_size = pixel.size();
  texture_desc.pixels = pixel.data();
  gneiss_texture texture{};
  gneiss_texture foreign{};
  if (resources.create_texture(texture_desc, &texture) != GNEISS_SUCCESS ||
      other.create_texture(texture_desc, &foreign) != GNEISS_SUCCESS) {
    return 1;
  }
  gneiss_material_desc desc = GNEISS_MATERIAL_DESC_INIT;
  desc.base_color_texture = texture;
  desc.metallic_roughness_texture = texture;
  desc.normal_texture = texture;
  desc.occlusion_texture = texture;
  desc.emissive_texture = texture;
  desc.normal_scale = 0.5F;
  desc.occlusion_strength = 0.25F;
  desc.emissive[1] = 0.75F;
  gneiss_material material{};
  if (resources.create_material(desc, &material) != GNEISS_SUCCESS) {
    return 2;
  }
  const auto* value = resources.get_material(material);
  if (value == nullptr || value->normal_scale != 0.5F || value->occlusion_strength != 0.25F ||
      value->emissive[1] != 0.75F) {
    return 3;
  }
  for (const auto handle : value->texture_handles()) {
    if (handle != texture) {
      return 4;
    }
  }
  if (resources.destroy_material(material) != GNEISS_SUCCESS ||
      resources.destroy_material(material) != GNEISS_ERROR_INVALID_HANDLE ||
      resources.get_texture(texture) == nullptr) {
    return 5;
  }
  const std::array slots{
      &gneiss_material_desc::base_color_texture, &gneiss_material_desc::metallic_roughness_texture,
      &gneiss_material_desc::normal_texture, &gneiss_material_desc::occlusion_texture,
      &gneiss_material_desc::emissive_texture};
  for (const auto slot : slots) {
    desc.*slot = foreign;
    if (resources.create_material(desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
      return 6;
    }
    desc.*slot = texture;
  }
  desc.normal_scale = std::numeric_limits<float>::quiet_NaN();
  if (resources.create_material(desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 7;
  }
  desc.normal_scale = -0.5F;
  if (resources.create_material(desc, &material) != GNEISS_SUCCESS ||
      resources.get_material(material)->normal_scale != -0.5F ||
      resources.destroy_material(material) != GNEISS_SUCCESS) {
    return 13;
  }
  desc.normal_scale = 1.0F;
  desc.occlusion_strength = 1.1F;
  if (resources.create_material(desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 8;
  }
  desc.occlusion_strength = 1.0F;
  desc.emissive[2] = std::numeric_limits<float>::infinity();
  if (resources.create_material(desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 9;
  }
  legacy_material_desc old{
      sizeof(legacy_material_desc), 0U, 1.0F, 1.0F, 1.0F, 1.0F, texture, 0.0F, 1.0F};
  static_assert(sizeof(old) == GNEISS_MATERIAL_DESC_VERSION_1_SIZE);
  // create_material 只按 struct_size 读取旧布局；Sanitizer 验证没有读取尾部新字段。
  const auto& legacy = *reinterpret_cast<const gneiss_material_desc*>(&old);
  if (resources.create_material(legacy, &material) != GNEISS_SUCCESS) {
    return 10;
  }
  value = resources.get_material(material);
  if (value->normal_scale != 1.0F || value->occlusion_strength != 1.0F ||
      value->normal_texture != GNEISS_NULL_TEXTURE || value->emissive[0] != 0.0F) {
    return 11;
  }
  if (resources.destroy_material(material) != GNEISS_SUCCESS) {
    return 12;
  }
  desc = GNEISS_MATERIAL_DESC_INIT;
  desc.struct_size = GNEISS_MATERIAL_DESC_VERSION_1_SIZE + 1U;
  if (resources.create_material(desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 13;
  }
  desc = GNEISS_MATERIAL_DESC_INIT;
  desc.normal_texture = texture;
  if (resources.destroy_texture(texture) != GNEISS_SUCCESS ||
      resources.create_material(desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT ||
      resources.live_resource_count() != 0U) {
    return 14;
  }
  return other.destroy_texture(foreign) == GNEISS_SUCCESS ? 0 : 15;
}
