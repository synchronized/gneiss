// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
namespace gneiss::engine {
class asset_manager;
/** 所属线程组合并装配场景；视觉资源异步加载，不等待 GPU。资产服务仅在调用期间借用。 */
GNEISS_API result<scene_instantiation_result, scene_diagnostic>
instantiate_scene(world&, asset_manager&, ref<const scene>, std::size_t source_bytes = 0);
/** 显式资源装配入口；只消费缓存或传入引用，失败不留下对象。 */
GNEISS_API result<scene_node_id>
create_mesh_renderer_node(world&, asset_manager&, scene_instance_id,
                          const mesh_renderer_node_creation&,
                          const scene_mesh_binding* prepared = nullptr);
/** 只查询已加载资源；URI 不变时保留当前引用，失败不改变组件。 */
GNEISS_API error set_mesh_renderer(world&, asset_manager&, scene_instance_id, scene_node_id,
                                   std::string_view mesh_uri, std::string_view material_uri);
/** 捕获 ECS 的资源引用，不访问加载缓存。 */
GNEISS_API result<scene_mesh_binding> capture_mesh_binding(world&, scene_instance_id,
                                                           scene_node_id);
/** 校验 expected 后替换引用；资源服务非空时同时校验来源及后端归属，不执行加载。 */
GNEISS_API error replace_mesh_binding(world&, scene_instance_id, scene_node_id,
                                      const scene_mesh_binding& expected,
                                      const scene_mesh_binding& replacement,
                                      guid component_uuid = {}, asset_manager* resources = nullptr);
}
