// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/types.hpp"
#include "engine/resource/texture/texture_description.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace gneiss::engine {
/** 材质纯值语义；资产与渲染共用，不持有资源或后端对象。 */
enum class material_alpha_mode : std::uint32_t { opaque, mask, blend };

/** 描述数据和运行期资源共用的材质参数；不包含纹理 URI 或 RID。 */
struct material_parameters {
  static constexpr std::string_view type_name = "material_parameters";
  static constexpr guid type_guid = guid::literal("f9f14c75-5ed5-457f-b09d-9411dc9a473c");
  math::vector4 color{1.0F, 1.0F, 1.0F, 1.0F};
  float metallic{};
  float roughness{1.0F};
  float normal_scale{1.0F};
  float occlusion_strength{1.0F};
  math::vector3 emissive{};
  material_alpha_mode alpha_mode{material_alpha_mode::opaque};
  bool double_sided{};
  float alpha_cutoff{0.5F};
  std::array<texture_sampling, 5> sampling{};
};

/** 纯 CPU 材质描述；纹理依赖使用 URI，不包含资源句柄。 */
struct material_description final {
  inline static constexpr std::string_view type_name = "material_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::literal("cc6df631-2727-40dc-a3ad-fce9a57d4909")};
  material_parameters parameters;
  std::array<std::string, 5> texture_uris;
};
/** 材质参数的领域约束，供文件、编辑提交和渲染资源采用复用。 */
[[nodiscard]] GNEISS_API serialization::diagnostic
validate_material_parameters(const material_parameters&);
[[nodiscard]] GNEISS_API serialization::diagnostic
validate_material_description(const material_description&);
}
