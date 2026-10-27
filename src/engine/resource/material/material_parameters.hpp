// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/texture/texture_types.hpp"

#include "engine/resource/material/material_types.hpp"

#include <array>
#include <cstdint>

namespace gneiss::engine {

/** 描述数据和运行期资源共用的材质参数；不包含纹理 URI 或 RID。 */
struct material_parameters {
  std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
  float metallic{};
  float roughness{1.0F};
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  std::array<float, 3> emissive{};
  gneiss::engine::material_alpha_mode alpha_mode{gneiss::engine::material_alpha_mode::opaque};
  bool double_sided{};
  float alpha_cutoff{0.5F};
  std::array<gneiss::engine::texture_sampling, 5> sampling{};
};

} // namespace gneiss::engine
