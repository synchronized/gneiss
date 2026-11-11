// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include <optional>
namespace gneiss {
class world;
/** 只读取所选相机，不枚举可渲染对象。无相机返回空值；限 World 所属线程。 */
[[nodiscard]] GNEISS_API result<std::optional<render_camera_snapshot>>
select_render_camera(world&, std::uint32_t width, std::uint32_t height,
                     const view_camera* override_camera = nullptr);
}
