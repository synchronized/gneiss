// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_HPP_
#define GNEISS_RENDER_HPP_

#include <gneiss/core/result.hpp>
#include <gneiss/render.h>

#include <exception>
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

using mesh_vertex = gneiss_mesh_vertex;
using mesh_normal = gneiss_mesh_normal;
using mesh_tangent = gneiss_mesh_tangent;
using mesh_uv = gneiss_mesh_uv;
using mesh_color = gneiss_mesh_color;
using texture_sampling = gneiss_texture_sampling;
using mesh_desc = gneiss_mesh_desc;
using material_desc = gneiss_material_desc;
using texture_desc = gneiss_texture_desc;
using ui_vertex = gneiss_ui_vertex;
using ui_draw_command = gneiss_ui_draw_command;
using ui_draw_list_desc = gneiss_ui_draw_list_desc;
using debug_line = gneiss_debug_line;
using debug_draw_list_desc = gneiss_debug_draw_list_desc;
using camera = gneiss_camera;
using camera_desc = gneiss_camera_desc;
using mesh_renderer = gneiss_mesh_renderer;

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
                                     render_resource_owner& output) noexcept {
    std::uint64_t handle{};
    const auto status = from_native(Create(application, &desc, &handle));
    if (status.failed()) {
      return status;
    }
    const auto closed = output.reset();
    if (closed.failed()) {
      (void)Destroy(application, handle);
      return closed;
    }
    output.application_ = application;
    output.handle_ = handle;
    return result::success;
  }
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
  [[nodiscard]] result reset() noexcept {
    if (handle_ == 0U) {
      return result::success;
    }
    const auto status = from_native(Destroy(application_, handle_));
    if (status.failed() && status != result::invalid_handle) {
      return status;
    }
    handle_ = 0U;
    application_ = GNEISS_NULL_APPLICATION;
    return result::success;
  }

private:
  void reset_or_terminate() noexcept {
    if (reset().failed()) {
      std::terminate();
    }
  }
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

#endif
