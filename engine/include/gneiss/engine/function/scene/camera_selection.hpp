// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/function/scene/entity_id.hpp>
#include <gneiss/engine/core/base/result.hpp>
namespace gneiss {
class world;
/** 设置默认渲染相机；限 World 所属线程，实体必须包含相机组件。 */
[[nodiscard]] GNEISS_ENGINE_API error set_active_camera(world&, entity_id);
/** 查询默认渲染相机；删除对象或相机组件后返回 not_ready，输出置零。 */
[[nodiscard]] GNEISS_ENGINE_API error get_active_camera(world&, entity_id&);
}
