// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_OBJECT_UNIFORM_H_
#define GNEISS_RENDER_GRANIT_OBJECT_UNIFORM_H_

#include "render/camera_math.h"

#include <gneiss/render.h>
#include <gneiss/scene.h>

#include <array>
#include <cstdint>

namespace gneiss::application_internal {

/** 与 Vertex Shader std140 布局一致的逐对象数据。 */
struct alignas(16) object_uniform final {
  render_internal::matrix4 model_view_projection;
  render_internal::matrix4 normal;
  std::array<float, 4> color;
};

/** 计算满足设备动态 Uniform Offset 对齐要求的对象步长。 */
[[nodiscard]] bool calculate_uniform_stride(std::uint64_t alignment,
                                            std::uint64_t& stride) noexcept;

/** 从相机、对象 Transform 与材质颜色构造 GPU 逐对象数据。 */
[[nodiscard]] bool build_object_uniform(const render_internal::matrix4& view,
                                        const render_internal::matrix4& projection,
                                        const gneiss_transform& transform,
                                        const std::array<float, 4>& color,
                                        object_uniform& output) noexcept;

} // namespace gneiss::application_internal

#endif
