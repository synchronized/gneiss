// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/resource/scene.hpp"
namespace gneiss::engine {
class asset_manager;
/** 已创建的场景及其视觉加载清单；资源身份已预留，内容加载由调用方调度。 */
struct scene_instantiation_result {
  scene_instance_id instance;
  std::size_t node_count{};
  std::vector<game_object> roots;
  std::vector<scene_object_binding> objects;
  std::vector<render_resource_source> assets;
  std::map<std::string, render_resource_ref, std::less<>> bindings;
};
/** 向既有场景追加完整组合结果；parent 为空时创建场景根。
 * 不执行 IO，不修改已有对象；失败只销毁本次创建的对象。全部访问限 World 所属线程。
 * 回执地址只在本次调用内唯一，多次实例化使用各自的回执。 */
GNEISS_API result<scene_instantiation_result> instantiate(scene_instance destination,
                                                          const composed_scene&,
                                                          const scene_resource_bindings&,
                                                          game_object parent = {});

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
