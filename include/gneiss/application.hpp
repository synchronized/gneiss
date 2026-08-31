// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_HPP_
#define GNEISS_APPLICATION_HPP_

#include <gneiss/application.h>
#include <gneiss/core/result.hpp>
#include <gneiss/render.hpp>

#include <cstdint>
#include <utility>

namespace gneiss {

/** 独占拥有 Application 的 RAII 包装；只允许在创建线程访问。 */
class application final {
public:
  application() noexcept = default;
  ~application() noexcept { reset(); }

  application(const application&) = delete;
  application& operator=(const application&) = delete;
  application(application&& other) noexcept
      : handle_(std::exchange(other.handle_, GNEISS_NULL_APPLICATION)) {}
  application& operator=(application&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, GNEISS_NULL_APPLICATION);
    }
    return *this;
  }

  [[nodiscard]] static result create(const gneiss_application_desc& desc,
                                     application& out_application) noexcept {
    gneiss_application handle = GNEISS_NULL_APPLICATION;
    const auto native_result = gneiss_application_create(&desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_application.reset();
      out_application.handle_ = handle;
    }
    return from_native(native_result);
  }

  [[nodiscard]] bool is_valid() const noexcept { return handle_ != GNEISS_NULL_APPLICATION; }
  [[nodiscard]] gneiss_application get() const noexcept { return handle_; }
  [[nodiscard]] result run(std::uint64_t max_frame_count = 0) noexcept {
    return from_native(gneiss_application_run(handle_, max_frame_count));
  }
  [[nodiscard]] result request_exit() noexcept {
    return from_native(gneiss_application_request_exit(handle_));
  }
  [[nodiscard]] result set_paused(bool is_paused) noexcept {
    return from_native(gneiss_application_set_paused(handle_, is_paused ? UINT8_C(1) : UINT8_C(0)));
  }
  [[nodiscard]] result get_world(gneiss_world& out_world) const noexcept {
    return from_native(gneiss_application_get_world(handle_, &out_world));
  }
  [[nodiscard]] result create_mesh(const mesh_desc& desc, mesh_id& out_mesh) noexcept {
    gneiss_mesh handle = GNEISS_NULL_MESH;
    const auto native_result = gneiss_mesh_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_mesh = mesh_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_mesh(mesh_id mesh) noexcept {
    return from_native(gneiss_mesh_destroy(handle_, mesh.get()));
  }
  [[nodiscard]] result create_material(const material_desc& desc,
                                       material_id& out_material) noexcept {
    gneiss_material handle = GNEISS_NULL_MATERIAL;
    const auto native_result = gneiss_material_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_material = material_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_material(material_id material) noexcept {
    return from_native(gneiss_material_destroy(handle_, material.get()));
  }
  [[nodiscard]] result create_texture(const texture_desc& desc, texture_id& out_texture) noexcept {
    gneiss_texture handle = GNEISS_NULL_TEXTURE;
    const auto native_result = gneiss_texture_create(handle_, &desc, &handle);
    if (native_result == GNEISS_SUCCESS) {
      out_texture = texture_id{handle};
    }
    return from_native(native_result);
  }
  [[nodiscard]] result destroy_texture(texture_id texture) noexcept {
    return from_native(gneiss_texture_destroy(handle_, texture.get()));
  }
  [[nodiscard]] result submit_ui_draw_list(const ui_draw_list_desc& desc) noexcept {
    return from_native(gneiss_application_submit_ui_draw_list(handle_, &desc));
  }
  [[nodiscard]] result submit_debug_draw_list(const debug_draw_list_desc& desc) noexcept {
    return from_native(gneiss_application_submit_debug_draw_list(handle_, &desc));
  }
  [[nodiscard]] result log(const gneiss_log_message& message) noexcept {
    return from_native(gneiss_application_log(handle_, &message));
  }

  void reset() noexcept {
    if (handle_ != GNEISS_NULL_APPLICATION) {
      (void)gneiss_application_destroy(handle_);
      handle_ = GNEISS_NULL_APPLICATION;
    }
  }

private:
  gneiss_application handle_ = GNEISS_NULL_APPLICATION;
};

} // namespace gneiss

#endif
