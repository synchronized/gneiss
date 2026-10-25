// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/render_resource_service.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

int main() {
  using gneiss::engine::render_resource_service;
  render_resource_service resources;
  render_resource_service other;
  // 原生路径同样拒绝非法枚举，不能依赖 C 入口代替服务校验。
  gneiss::engine::material_resource native;
  gneiss::engine::rid native_handle{};
  native.parameters.alpha_mode = static_cast<gneiss::engine::material_alpha_mode>(UINT32_MAX);
  if (resources.create_material(native, &native_handle) != gneiss::engine::result::invalid_argument)
    return 30;
  native.parameters.alpha_mode = gneiss::engine::material_alpha_mode::opaque;
  native.parameters.sampling[2].address_v =
      static_cast<gneiss::engine::texture_address>(UINT32_MAX);
  if (resources.create_material(native, &native_handle) != gneiss::engine::result::invalid_argument)
    return 31;
  native.parameters.sampling[2] = {};
  if (resources.create_material(native, &native_handle) != gneiss::engine::result::success ||
      resources.destroy_material(native_handle) != gneiss::engine::result::success)
    return 32;
  constexpr std::array<std::uint8_t, 4> pixel{128U, 128U, 255U, 255U};
  gneiss::engine::texture_view texture_desc;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixels = pixel;
  gneiss::engine::rid texture{};
  gneiss::engine::rid foreign{};
  if (resources.create_texture(texture_desc, &texture) != gneiss::engine::result::success ||
      other.create_texture(texture_desc, &foreign) != gneiss::engine::result::success) {
    return 1;
  }
  gneiss::engine::material_resource desc;
  desc.base_color_texture = texture;
  desc.metallic_roughness_texture = texture;
  desc.normal_texture = texture;
  desc.occlusion_texture = texture;
  desc.emissive_texture = texture;
  desc.parameters.normal_scale = 0.5F;
  desc.parameters.occlusion_strength = 0.25F;
  desc.parameters.emissive[1] = 0.75F;
  gneiss::engine::rid material{};
  if (resources.create_material(desc, &material) != gneiss::engine::result::success) {
    return 2;
  }
  const auto* value = resources.get_material(material);
  if (value == nullptr || value->parameters.normal_scale != 0.5F ||
      value->parameters.occlusion_strength != 0.25F || value->parameters.emissive[1] != 0.75F) {
    return 3;
  }
  for (const auto handle : value->texture_handles()) {
    if (handle != texture) {
      return 4;
    }
  }
  if (resources.destroy_material(material) != gneiss::engine::result::success ||
      resources.destroy_material(material) != gneiss::engine::result::invalid_handle ||
      resources.get_texture(texture) == nullptr) {
    return 5;
  }
  const std::array slots{&gneiss::engine::material_resource::base_color_texture,
                         &gneiss::engine::material_resource::metallic_roughness_texture,
                         &gneiss::engine::material_resource::normal_texture,
                         &gneiss::engine::material_resource::occlusion_texture,
                         &gneiss::engine::material_resource::emissive_texture};
  for (const auto slot : slots) {
    desc.*slot = foreign;
    if (resources.create_material(desc, &material) != gneiss::engine::result::invalid_argument) {
      return 6;
    }
    desc.*slot = texture;
  }
  desc.parameters.normal_scale = std::numeric_limits<float>::quiet_NaN();
  if (resources.create_material(desc, &material) != gneiss::engine::result::invalid_argument) {
    return 7;
  }
  desc.parameters.normal_scale = -0.5F;
  if (resources.create_material(desc, &material) != gneiss::engine::result::success ||
      resources.get_material(material)->parameters.normal_scale != -0.5F ||
      resources.destroy_material(material) != gneiss::engine::result::success) {
    return 13;
  }
  desc.parameters.normal_scale = 1.0F;
  desc.parameters.occlusion_strength = 1.1F;
  if (resources.create_material(desc, &material) != gneiss::engine::result::invalid_argument) {
    return 8;
  }
  desc.parameters.occlusion_strength = 1.0F;
  desc.parameters.emissive[2] = std::numeric_limits<float>::infinity();
  if (resources.create_material(desc, &material) != gneiss::engine::result::invalid_argument) {
    return 9;
  }
  desc = {};
  desc.parameters.alpha_mode = gneiss::engine::material_alpha_mode::blend;
  desc.parameters.double_sided = 1;
  desc.parameters.alpha_cutoff = 1.5F;
  desc.base_color_texture = texture;
  desc.parameters.sampling[0].uv_set = 1;
  desc.parameters.sampling[0].address_u = gneiss::engine::texture_address::mirror;
  if (resources.create_material(desc, &material) != gneiss::engine::result::success)
    return 16;
  value = resources.get_material(material);
  if (value->parameters.alpha_mode != gneiss::engine::material_alpha_mode::blend ||
      value->parameters.double_sided != 1U || value->parameters.alpha_cutoff != 1.5F ||
      value->uv1_mask() != 1U ||
      value->parameters.sampling[0].address_u != gneiss::engine::texture_address::mirror)
    return 17;
  if (resources.destroy_material(material) != gneiss::engine::result::success)
    return 18;
  const auto rejects_invalid = [&](auto& field) {
    const auto saved = field;
    field = static_cast<std::remove_reference_t<decltype(field)>>(UINT32_MAX);
    const bool rejected =
        resources.create_material(desc, &material) == gneiss::engine::result::invalid_argument;
    field = saved;
    return rejected;
  };
  if (!rejects_invalid(desc.parameters.alpha_mode) ||
      !rejects_invalid(desc.parameters.sampling[0].uv_set) ||
      !rejects_invalid(desc.parameters.sampling[0].mag_filter) ||
      !rejects_invalid(desc.parameters.sampling[0].min_filter) ||
      !rejects_invalid(desc.parameters.sampling[0].mip_filter) ||
      !rejects_invalid(desc.parameters.sampling[0].address_u) ||
      !rejects_invalid(desc.parameters.sampling[0].address_v))
    return 19;
  desc.parameters.alpha_cutoff = std::numeric_limits<float>::quiet_NaN();
  if (resources.create_material(desc, &material) != gneiss::engine::result::invalid_argument)
    return 20;
  desc = {};
  desc.normal_texture = texture;
  if (resources.destroy_texture(texture) != gneiss::engine::result::success ||
      resources.create_material(desc, &material) != gneiss::engine::result::invalid_argument ||
      resources.live_resource_count() != 0U) {
    return 14;
  }
  return other.destroy_texture(foreign) == gneiss::engine::result::success ? 0 : 15;
}
