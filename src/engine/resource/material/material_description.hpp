// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/resource/texture/texture_types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace gneiss::engine {
/** 材质纯值语义；资产与渲染共用，不持有资源或后端对象。 */
enum class material_alpha_mode : std::uint32_t { opaque, mask, blend };

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

/** 纯 CPU 材质描述；纹理依赖使用 URI，不包含资源句柄。 */
struct material_description final {
  inline static constexpr std::string_view type_name = "material_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::from_bytes({0xcc, 0x6d, 0xf6, 0x31, 0x27, 0x27, 0x40, 0xdc, 0xa3, 0xad, 0xfc,
                                0xe9, 0xa5, 0x7d, 0x49, 0x09})};
  material_parameters parameters;
  std::array<std::string, 5> texture_uris;
};
}
