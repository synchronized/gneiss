// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/resource/types/material_types.hpp"

#include <array>
#include <cstdint>

namespace gneiss::asset_internal {

/** 资产解析得到的材质参数；不包含纹理 RID，纹理依赖单独用 URI 表达。 */
struct material_parameters {
  std::array<float, 4> color{};
  float metallic{};
  float roughness{1.0F};
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  std::array<float, 3> emissive{};
  asset_internal::material_alpha_mode alpha_mode{asset_internal::material_alpha_mode::opaque};
  bool double_sided{};
  float alpha_cutoff{0.5F};
  std::array<asset_internal::texture_sampling, 5> sampling{};
};

} // namespace gneiss::asset_internal
