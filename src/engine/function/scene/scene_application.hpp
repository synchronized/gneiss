// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/scene.hpp"
#include <functional>
namespace gneiss::engine {
/** 外部协调方持有的地址关联，不保存作者描述或组件副本。 */
struct scene_object_association {
  entity_id entity;
};
/** 可选的已有组件更新准备；不保存回调，不得在回调中修改 World。 */
using scene_component_preparer = std::function<result<component_description>(
    const composed_object&, entity_id, const component_description&)>;
/** 把有效对象差异应用到场景，不读取文档、文件或 Prefab 来源。
 * 空场景及空基线用于首次创建；存活地址保持实体身份。
 * false 表示基线不再匹配，且尚未开始修改；错误不保证回滚，调用方必须使同步状态失效。
 * 只允许 World 所在线程；关联及资源仅在调用期间借用。
 */
[[nodiscard]] GNEISS_API result<bool>
apply_scene(world&, scene_instance_id,
            std::map<object_address, scene_object_association>& associations,
            const composed_scene& previous, const composed_scene& next,
            const scene_resource_bindings& assets, const scene_component_preparer& prepare = {});
} // namespace gneiss::engine
