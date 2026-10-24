// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_HPP_
#define GNEISS_RENDER_HPP_

#include "engine/core/math/camera_settings.hpp"
#include "engine/values/core/result.hpp"
#include <gneiss/engine/render.h>

#include <array>
#include <cstdint>
#include <exception>
#include <span>
#include <utility>

namespace gneiss {

class mesh_id final {
public:
  constexpr mesh_id() noexcept = default;
  explicit constexpr mesh_id(gneiss_mesh value) noexcept : value_(value) {}
  [[nodiscard]] constexpr gneiss_mesh get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_MESH; }

private:
  gneiss_mesh value_ = GNEISS_NULL_MESH;
};

class material_id final {
public:
  constexpr material_id() noexcept = default;
  explicit constexpr material_id(gneiss_material value) noexcept : value_(value) {}
  [[nodiscard]] constexpr gneiss_material get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_MATERIAL; }

private:
  gneiss_material value_ = GNEISS_NULL_MATERIAL;
};

class texture_id final {
public:
  constexpr texture_id() noexcept = default;
  explicit constexpr texture_id(gneiss_texture value) noexcept : value_(value) {}
  [[nodiscard]] constexpr gneiss_texture get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_TEXTURE; }

private:
  gneiss_texture value_ = GNEISS_NULL_TEXTURE;
};

/** 独立 C++ 顶点属性；由 Mesh 描述在调用期间借用。 */
struct mesh_vertex {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float u = 0.0F;
  float v = 0.0F;
};
[[nodiscard]] constexpr gneiss_mesh_vertex to_native(const mesh_vertex& value) noexcept {
  return {.x = value.x, .y = value.y, .z = value.z, .u = value.u, .v = value.v};
}

/** 独立 C++ 顶点属性；由 Mesh 描述在调用期间借用。 */
struct mesh_normal {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};
[[nodiscard]] constexpr gneiss_mesh_normal to_native(const mesh_normal& value) noexcept {
  return {.x = value.x, .y = value.y, .z = value.z};
}

/** 独立 C++ 顶点属性；由 Mesh 描述在调用期间借用。 */
struct mesh_tangent {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float w = 1.0F;
};
[[nodiscard]] constexpr gneiss_mesh_tangent to_native(const mesh_tangent& value) noexcept {
  return {.x = value.x, .y = value.y, .z = value.z, .w = value.w};
}

/** 独立 C++ 顶点属性；由 Mesh 描述在调用期间借用。 */
struct mesh_uv {
  float u = 0.0F;
  float v = 0.0F;
};
[[nodiscard]] constexpr gneiss_mesh_uv to_native(const mesh_uv& value) noexcept {
  return {.u = value.u, .v = value.v};
}

/** 独立 C++ 顶点属性；由 Mesh 描述在调用期间借用。 */
struct mesh_color {
  float r = 1.0F;
  float g = 1.0F;
  float b = 1.0F;
  float a = 1.0F;
};
[[nodiscard]] constexpr gneiss_mesh_color to_native(const mesh_color& value) noexcept {
  return {.r = value.r, .g = value.g, .b = value.b, .a = value.a};
}

// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 数值，未知枚举交给 C 边界校验。
enum class texture_format : std::uint32_t { rgba8_unorm = 1 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 数值，未知枚举交给 C 边界校验。
enum class texture_color_space : std::uint32_t { linear = 1, srgb = 2 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 数值，未知枚举交给 C 边界校验。
enum class material_alpha_mode : std::uint32_t { opaque = 0, mask = 1, blend = 2 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 数值，未知枚举交给 C 边界校验。
enum class texture_filter : std::uint32_t { nearest = 0, linear = 1 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 数值，未知枚举交给 C 边界校验。
enum class texture_mip_filter : std::uint32_t { none = 0, nearest = 1, linear = 2 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 数值，未知枚举交给 C 边界校验。
enum class texture_address : std::uint32_t { repeat = 0, clamp = 1, mirror = 2 };

// 默认成员初始化支持省略字段的 designated 初始化，避免消费者触发
// -Wmissing-designated-field-initializers；不是可删除的构造器初始化冗余。
// NOLINTBEGIN(readability-redundant-member-init)
/** 像素只在创建调用期间借用；格式与颜色空间由资源服务验证。 */
struct texture_desc {
  texture_format format = texture_format::rgba8_unorm;
  texture_color_space color_space = texture_color_space::srgb;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t row_stride_bytes = 0;
  std::span<const std::uint8_t> pixels = {};
};
/** 每个材质纹理槽的采样值；不持有资源。 */
struct texture_sampling {
  std::uint32_t uv_set = 0;
  texture_filter mag_filter = texture_filter::linear;
  texture_filter min_filter = texture_filter::linear;
  texture_mip_filter mip_filter = texture_mip_filter::linear;
  texture_address address_u = texture_address::repeat;
  texture_address address_v = texture_address::repeat;
};
/** 材质值；纹理必须属于同一 Application，不转移或延长纹理所有权。 */
struct material_desc {
  float red = 1.0F;
  float green = 1.0F;
  float blue = 1.0F;
  float alpha = 1.0F;
  texture_id base_color_texture{};
  float metallic = 0.0F;
  float roughness = 1.0F;
  texture_id metallic_roughness_texture{};
  texture_id normal_texture{};
  texture_id occlusion_texture{};
  texture_id emissive_texture{};
  float normal_scale = 1.0F;
  float occlusion_strength = 1.0F;
  std::array<float, 3> emissive{};
  material_alpha_mode alpha_mode = material_alpha_mode::opaque;
  bool double_sided = false;
  float alpha_cutoff = 0.5F;
  std::array<texture_sampling, 5> sampling{};
};
[[nodiscard]] constexpr gneiss_texture_sampling to_native(const texture_sampling& value) noexcept;
/** 返回借用相同像素的 C 描述；使用期间 pixels 必须保持有效。 */
[[nodiscard]] constexpr gneiss_texture_desc to_native(const texture_desc& value) noexcept;
/** 逐字段转换，不依赖 C/C++ 布局；纹理仍只借用。 */
[[nodiscard]] constexpr gneiss_material_desc to_native(const material_desc& value) noexcept;

/** 数组借用至 create 返回；转换时逐元素复制属性，索引直接借用。 */
struct mesh_desc {
  std::span<const mesh_vertex> vertices = {};
  std::span<const mesh_normal> normals = {};
  std::span<const std::uint32_t> indices = {};
  std::span<const mesh_tangent> tangents = {};
  std::span<const mesh_uv> uv1 = {};
  std::span<const mesh_color> colors = {};
};
/** 即时 UI 顶点，颜色按每字节 RGBA 打包。 */
// NOLINTEND(readability-redundant-member-init)
/** 迁移期 C 边界转换；原生相机配置不依赖 ABI 布局。 */
[[nodiscard]] constexpr gneiss_camera_desc to_native(const engine::camera_settings& value) noexcept;
[[nodiscard]] constexpr engine::camera_settings
from_native(const gneiss_camera_desc& value) noexcept;

} // namespace gneiss

#include "engine/values/detail/render.inl"

#endif
