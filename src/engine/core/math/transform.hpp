// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include <algorithm>
#include <string_view>

#include "engine/core/math/functions.hpp"

#include <glm/ext/matrix_transform.hpp>

namespace gneiss::engine {
/** 纯值 TRS；四元数分量访问及序列化顺序为 xyzw，构造顺序为 wxyz，不持有场景或后端状态。 */
struct transform {
  inline static constexpr std::string_view type_name = "transform";
  inline static constexpr engine::guid type_guid{
      guid::from_bytes({0x69, 0x64, 0x4f, 0x20, 0xb2, 0xd2, 0x4e, 0x48, 0x8c, 0x74, 0x91, 0xf4,
                        0xf9, 0x52, 0xec, 0x2d})};
  vector3 translation{0.0F};
  quaternion rotation{1.0F, 0.0F, 0.0F, 0.0F};
  vector3 scale{1.0F};
};
inline bool is_valid_transform(const transform& value) noexcept {
  return is_finite(value.translation) && is_finite(value.rotation) && is_finite(value.scale) &&
         std::abs(glm::length(value.rotation) - 1.0F) <= 1.0e-4F &&
         std::ranges::all_of(components(value.scale),
                             [](float v) { return std::abs(v) >= 1.0e-6F; });
}
/** 延续场景树的 TRS 分量组合语义，不新增剪切表示。 */
inline transform compose_transform(const transform& parent, const transform& local) noexcept {
  return {parent.translation + parent.rotation * (parent.scale * local.translation),
          parent.rotation * local.rotation, parent.scale * local.scale};
}
inline matrix4 model_matrix(const transform& value) noexcept {
  return glm::translate(matrix4{1.0F}, value.translation) *
         glm::scale(glm::mat4_cast(value.rotation), value.scale);
}
/** 使用逆转置缩放与旋转变换单位法线，并重新归一化。 */
[[nodiscard]] vector3 transform_normal(const vector3& normal, const transform& value) noexcept;
/** 从 TRS 构造 Model 与逆转置 Normal Matrix；拒绝非有限值、零缩放或无效旋转。 */
[[nodiscard]] bool model_and_normal_matrices(const transform& value, matrix4& model,
                                             matrix4& normal) noexcept;
} // namespace gneiss::engine
