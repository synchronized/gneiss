// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_HPP_
#define GNEISS_RENDER_HPP_

#include <gneiss/engine/core/result.hpp>
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
struct ui_vertex {
  std::array<float, 2> position{};
  std::array<float, 2> uv{};
  std::uint32_t color_rgba8 = 0;
};
/** 索引和顶点偏移相对当次提交；纹理只借用。 */
struct ui_draw_command {
  texture_id texture{};
  std::array<float, 2> clip_min{};
  std::array<float, 2> clip_max{};
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
  std::uint32_t vertex_offset = 0;
};
/** 数组借用至提交返回；成功提交的数据只在当前帧有效。 */
struct ui_draw_list_desc {
  float display_width = 0.0F;
  float display_height = 0.0F;
  float framebuffer_scale_x = 1.0F;
  float framebuffer_scale_y = 1.0F;
  std::span<const ui_vertex> vertices = {};
  std::span<const std::uint32_t> indices = {};
  std::span<const ui_draw_command> commands = {};
};
/** 世界调试线段，宽度为像素；数组借用至提交返回。 */
struct debug_line {
  std::array<float, 3> start{};
  std::array<float, 3> end{};
  std::uint32_t color_rgba8 = 0;
  float width = 1.0F;
  bool depth_test = true;
};
struct debug_draw_list_desc {
  std::span<const debug_line> lines = {};
};
[[nodiscard]] constexpr gneiss_ui_vertex to_native(const ui_vertex& value) noexcept;
[[nodiscard]] constexpr gneiss_ui_draw_command to_native(const ui_draw_command& value) noexcept;
[[nodiscard]] constexpr gneiss_debug_line to_native(const debug_line& value) noexcept;
/** 透视相机配置；默认视角约 60 度，裁剪面为 0.1 与 1000。 */
struct camera_desc {
  float vertical_field_of_view_radians = 1.04719755F;
  float near_plane = 0.1F;
  float far_plane = 1000.0F;
};
/** 兼容既有 set_camera 的主相机选择语义；新代码可用 configure_camera + set_active_camera。 */
struct camera {
  float vertical_field_of_view_radians = 1.04719755F;
  float near_plane = 0.1F;
  float far_plane = 1000.0F;
  bool is_primary = true;
};
/** 只借用资源标识，不延长 Mesh、Material 或其所属 Application 的寿命。 */
struct mesh_renderer {
  mesh_id mesh{};
  material_id material{};
};
// NOLINTEND(readability-redundant-member-init)
/** 显式 C 互操作；逐字段转换，不依赖内存布局。 */
[[nodiscard]] constexpr gneiss_camera_desc to_native(const camera_desc& value) noexcept;
[[nodiscard]] constexpr camera_desc from_native(const gneiss_camera_desc& value) noexcept;
[[nodiscard]] constexpr gneiss_camera to_native(const camera& value) noexcept;
[[nodiscard]] constexpr camera from_native(const gneiss_camera& value) noexcept;
[[nodiscard]] constexpr gneiss_mesh_renderer to_native(const mesh_renderer& value) noexcept {
  return {.mesh = value.mesh.get(), .material = value.material.get()};
}
[[nodiscard]] constexpr mesh_renderer from_native(const gneiss_mesh_renderer& value) noexcept {
  return {.mesh = mesh_id{value.mesh}, .material = material_id{value.material}};
}

namespace detail {
/** 仅保存父句柄与资源句柄；不得在创建线程之外销毁或移动覆盖资源。 */
template <typename Id, typename Desc, auto Create, auto Destroy> class render_resource_owner final {
public:
  render_resource_owner() noexcept = default;
  ~render_resource_owner() noexcept { reset_or_terminate(); }
  render_resource_owner(const render_resource_owner&) = delete;
  render_resource_owner& operator=(const render_resource_owner&) = delete;
  render_resource_owner(render_resource_owner&& other) noexcept
      : application_(std::exchange(other.application_, GNEISS_NULL_APPLICATION)),
        handle_(std::exchange(other.handle_, 0U)) {}
  render_resource_owner& operator=(render_resource_owner&& other) noexcept {
    if (this != &other) {
      reset_or_terminate();
      application_ = std::exchange(other.application_, GNEISS_NULL_APPLICATION);
      handle_ = std::exchange(other.handle_, 0U);
    }
    return *this;
  }
  /** 失败时保留 output；成功替换其原资源。 */
  [[nodiscard]] static result create(gneiss_application application, const Desc& desc,
                                     render_resource_owner& output) noexcept;
  [[nodiscard]] std::uint64_t get() const noexcept { return handle_; }
  /** 借用标识，不转移所有权；非零不保证父服务仍存活。 */
  [[nodiscard]] Id id() const noexcept { return Id{handle_}; }
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != 0U; }
  [[nodiscard]] gneiss_application owner() const noexcept { return application_; }
  /** 转移原始资源所有权；调用方须保存 owner() 并负责销毁。 */
  [[nodiscard]] Id release() noexcept {
    application_ = GNEISS_NULL_APPLICATION;
    return Id{std::exchange(handle_, 0U)};
  }
  /** 幂等关闭；父服务或 RID 已失效视为释放完成。其他失败保留句柄供重试。
   * 析构与移动覆盖无法返回错误，若违反所属线程等约束导致关闭失败则终止进程。 */
  [[nodiscard]] result reset() noexcept;

private:
  void reset_or_terminate() noexcept;
  gneiss_application application_{};
  std::uint64_t handle_{};
};
} // namespace detail

/** Mesh 独占所有权；操作和销毁须在所属 Application 线程。 */
using mesh =
    detail::render_resource_owner<mesh_id, mesh_desc, gneiss_mesh_create, gneiss_mesh_destroy>;
/** Material 独占所有权；不延长父 Application 或引用纹理的生命周期。 */
using material = detail::render_resource_owner<material_id, material_desc, gneiss_material_create,
                                               gneiss_material_destroy>;
/** Texture 独占所有权；操作和销毁须在所属 Application 线程。 */
using texture = detail::render_resource_owner<texture_id, texture_desc, gneiss_texture_create,
                                              gneiss_texture_destroy>;

} // namespace gneiss

#include <gneiss/engine/detail/render.inl>

#endif
