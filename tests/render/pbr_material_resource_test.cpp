// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/material_description.hpp"
#include "engine/function/render/render_resource_service.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
// 旧布局验证属于 ABI 适配器；资源数值、RID 与生命周期仍由服务验证。
gneiss_result create_material(gneiss::render_internal::render_resource_service& resources,
                              const gneiss_material_desc& desc, gneiss_material* output) {
  gneiss::render_internal::material_resource value;
  const auto converted = gneiss::api::read_material_description(desc, value);
  return converted == GNEISS_SUCCESS ? resources.create_material(value, output) : converted;
}
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
  if (create_material(resources, desc, &material) != GNEISS_SUCCESS) {
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
    if (create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
      return 6;
    }
    desc.*slot = texture;
  }
  desc.normal_scale = std::numeric_limits<float>::quiet_NaN();
  if (create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 7;
  }
  desc.normal_scale = -0.5F;
  if (create_material(resources, desc, &material) != GNEISS_SUCCESS ||
      resources.get_material(material)->normal_scale != -0.5F ||
      resources.destroy_material(material) != GNEISS_SUCCESS) {
    return 13;
  }
  desc.normal_scale = 1.0F;
  desc.occlusion_strength = 1.1F;
  if (create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 8;
  }
  desc.occlusion_strength = 1.0F;
  desc.emissive[2] = std::numeric_limits<float>::infinity();
  if (create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 9;
  }
  // 使用真实描述对象模拟旧调用方；尾部放入非法值，验证旧大小不会使用新增字段。
  gneiss_material_desc legacy = GNEISS_MATERIAL_DESC_INIT;
  legacy.struct_size = GNEISS_MATERIAL_DESC_VERSION_1_SIZE;
  legacy.base_color_texture = texture;
  legacy.normal_scale = std::numeric_limits<float>::quiet_NaN();
  legacy.normal_texture = foreign;
  legacy.alpha_mode = UINT32_MAX;
  if (create_material(resources, legacy, &material) != GNEISS_SUCCESS) {
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
  gneiss::render_internal::material_resource unchanged{.red = 0.125F};
  if (gneiss::api::read_material_description(desc, unchanged) != GNEISS_ERROR_INVALID_ARGUMENT ||
      unchanged.red != 0.125F || unchanged.normal_texture != GNEISS_NULL_TEXTURE) {
    return 22;
  }
  if (create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 13;
  }
  desc = GNEISS_MATERIAL_DESC_INIT;
  desc.alpha_mode = GNEISS_MATERIAL_ALPHA_BLEND;
  desc.double_sided = 1;
  desc.alpha_cutoff = 1.5F;
  desc.base_color_texture = texture;
  desc.sampling[0].uv_set = 1;
  desc.sampling[0].address_u = GNEISS_TEXTURE_ADDRESS_MIRROR;
  if (create_material(resources, desc, &material) != GNEISS_SUCCESS)
    return 16;
  value = resources.get_material(material);
  if (value->alpha_mode != GNEISS_MATERIAL_ALPHA_BLEND || value->double_sided != 1U ||
      value->alpha_cutoff != 1.5F || value->uv1_mask() != 1U ||
      value->sampling[0].address_u != GNEISS_TEXTURE_ADDRESS_MIRROR)
    return 17;
  if (resources.destroy_material(material) != GNEISS_SUCCESS)
    return 18;
  for (auto* field :
       {&desc.alpha_mode, &desc.double_sided, &desc.sampling[0].uv_set,
        &desc.sampling[0].mag_filter, &desc.sampling[0].min_filter, &desc.sampling[0].mip_filter,
        &desc.sampling[0].address_u, &desc.sampling[0].address_v}) {
    const auto saved = *field;
    *field = UINT32_MAX;
    if (create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT)
      return 19;
    *field = saved;
  }
  desc.alpha_cutoff = std::numeric_limits<float>::quiet_NaN();
  if (create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT)
    return 20;
  desc.struct_size = GNEISS_MATERIAL_DESC_VERSION_2_SIZE;
  if (create_material(resources, desc, &material) != GNEISS_SUCCESS ||
      resources.get_material(material)->alpha_mode != GNEISS_MATERIAL_ALPHA_OPAQUE ||
      resources.get_material(material)->uv1_mask() != 0U ||
      resources.destroy_material(material) != GNEISS_SUCCESS)
    return 21;
  desc = GNEISS_MATERIAL_DESC_INIT;
  desc.normal_texture = texture;
  if (resources.destroy_texture(texture) != GNEISS_SUCCESS ||
      create_material(resources, desc, &material) != GNEISS_ERROR_INVALID_ARGUMENT ||
      resources.live_resource_count() != 0U) {
    return 14;
  }
  return other.destroy_texture(foreign) == GNEISS_SUCCESS ? 0 : 15;
}
