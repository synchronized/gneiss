// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/function/render/camera.hpp>
#include <optional>
namespace gneiss {
class world;
/** 只读取所选相机，不枚举可渲染对象。无相机返回空值；限 World 所属线程。 */
[[nodiscard]] GNEISS_ENGINE_API result<std::optional<render_camera_snapshot>>
select_render_camera(world&, std::uint32_t width, std::uint32_t height,
                     const view_camera* override_camera = nullptr);
}
