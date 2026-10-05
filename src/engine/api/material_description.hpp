// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/render/render_resource_data.hpp"

#include <algorithm>

namespace gneiss::api {

/** 只读取调用方声明的版本字段；失败保留输出，数值与资源归属由 Render 校验。 */
[[nodiscard]] inline gneiss_result
read_material_description(const gneiss_material_desc& desc,
                          render_internal::material_resource& output) noexcept {
  if ((desc.struct_size != GNEISS_MATERIAL_DESC_VERSION_1_SIZE &&
       desc.struct_size != GNEISS_MATERIAL_DESC_VERSION_2_SIZE &&
       desc.struct_size < sizeof(gneiss_material_desc)) ||
      desc.reserved != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  render_internal::material_resource value{
      .red = desc.red,
      .green = desc.green,
      .blue = desc.blue,
      .alpha = desc.alpha,
      .base_color_texture = desc.base_color_texture,
      .metallic = desc.metallic,
      .roughness = desc.roughness,
  };
  if (desc.struct_size >= GNEISS_MATERIAL_DESC_VERSION_2_SIZE) {
    if (desc.reserved_2 != 0U) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    value.metallic_roughness_texture = desc.metallic_roughness_texture;
    value.normal_texture = desc.normal_texture;
    value.occlusion_texture = desc.occlusion_texture;
    value.emissive_texture = desc.emissive_texture;
    value.normal_scale = desc.normal_scale;
    value.occlusion_strength = desc.occlusion_strength;
    std::ranges::copy(desc.emissive, value.emissive.begin());
  }
  if (desc.struct_size >= sizeof(gneiss_material_desc)) {
    if (desc.reserved_3 != 0U) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    value.alpha_mode = desc.alpha_mode;
    value.double_sided = desc.double_sided;
    value.alpha_cutoff = desc.alpha_cutoff;
    std::ranges::copy(desc.sampling, value.sampling.begin());
  }
  output = value;
  return GNEISS_SUCCESS;
}

} // namespace gneiss::api
