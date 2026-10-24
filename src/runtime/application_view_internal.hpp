// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/result.hpp"
#include "engine/function/render/render_snapshot.hpp"
#include <gneiss/engine/application.h>

namespace gneiss::runtime {
/** 所属线程设置宿主视口相机的值副本；nullptr 恢复运行时主相机。
 * 不修改 World；设置保持至再次设置或 Application 销毁，截图使用同一选择。
 * 私有单视口接口，不安装为 SDK，不持有调用方指针。
 */
[[nodiscard]] GNEISS_API gneiss::engine::result
set_view_camera(gneiss_application application, const gneiss::engine::view_camera* camera) noexcept;
}
