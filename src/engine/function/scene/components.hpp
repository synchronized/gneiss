// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/camera.hpp"
#include "engine/core/result.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/material.hpp"
#include "engine/resource/mesh.hpp"

namespace gneiss {
/** 相机参数由组件校验；默认构造为有效透视相机，选择独立于组件。 */
class camera_component {
public:
  camera_component() = default;
  [[nodiscard]] static result<camera_component> create(const math::camera_settings& settings,
                                                       bool primary = false) {
    camera_component value;
    auto status = value.set_settings(settings);
    if (status.failed())
      return status;
    value.primary_hint = primary;
    return value;
  }
  [[nodiscard]] const math::camera_settings& settings() const noexcept { return settings_; }
  /** 非法参数返回 invalid_argument，保留原值；借用组件的修改限 World 所属线程。 */
  [[nodiscard]] error set_settings(const math::camera_settings& settings) noexcept {
    if (!math::is_valid_camera(settings))
      return error::invalid_argument;
    settings_ = settings;
    return error::success;
  }
  bool primary_hint{}; // 默认相机选择提示，不自动激活。
private:
  math::camera_settings settings_{1.04719755F, 0.1F, 1000.0F};
};
/** 组件直接保活运行资源，必须执行正常的构造、移动和析构。 */
struct mesh_component {
  ref<const gneiss::mesh> mesh{};
  ref<const gneiss::material> material{};
};
} // namespace gneiss
