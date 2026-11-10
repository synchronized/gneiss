// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/function/render/render_snapshot.hpp"
namespace gneiss::engine {
class world;
/** 在 World 所属线程提取渲染输入；结果持有资源强引用，不借用 ECS 数据。
 * camera 仅在调用期间借用；为空时使用 World 当前活动相机。 */
[[nodiscard]] GNEISS_API result<render_snapshot>
extract_render_snapshot(world&, std::uint32_t viewport_width, std::uint32_t viewport_height,
                        const view_camera* camera = nullptr);
}
