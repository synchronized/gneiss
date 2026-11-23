// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/export.hpp>
#include <gneiss/engine/function/scene/composition.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <gneiss/engine/resource/scene.hpp>
namespace gneiss {
/** 已创建的场景及其视觉加载清单；资源身份已预留，内容加载由调用方调度。 */
struct scene_instantiation_result {
  scene_instance instance;
  std::size_t node_count{};
  std::vector<game_object> roots;
  std::vector<scene_object_binding> objects;
  std::vector<render_resource_source> assets;
  std::map<std::string, render_resource_ref, std::less<>> bindings;
};
/** 向既有场景追加完整组合结果；parent 为空时创建场景根。
 * 不执行 IO，不修改已有对象；失败只销毁本次创建的对象。全部访问限 World 所属线程。
 * 回执地址只在本次调用内唯一，多次实例化使用各自的回执。 */
GNEISS_ENGINE_API result<scene_instantiation_result> instantiate(scene_instance destination,
                                                          const composed_scene&,
                                                          const scene_resource_bindings&,
                                                          game_object parent = {});

}
