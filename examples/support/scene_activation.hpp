// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "application/application.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/function/scene/world.hpp"
#include <cstdio>
namespace example {
/** 示例持有的装配回执与视觉批次；场景由 World 拥有，调用方决定何时卸载。 */
struct scene_activation {
  gneiss::engine::scene_instantiation_result instance;
  gneiss::engine::asset_batch_request visuals;
};
/** 内容已完成异步加载后调用；只做内存组合、实例化及提交视觉请求，不等待 I/O。 */
inline gneiss::engine::result<scene_activation>
activate_scene(gneiss::runtime::application& app, const gneiss::engine::scene_content& content) {
  using namespace gneiss::engine;
  if (!content.description)
    return error::invalid_argument;
  std::map<std::string, const prefab_description*, std::less<>> sources;
  for (const auto& [uri, dependency] : content.prefabs)
    sources.emplace(uri, dependency.description.get());
  auto composed = compose_scene(*content.description, sources);
  if (!composed)
    return composed.error();
  auto dependencies = scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  scene_activation output;
  if (!dependencies->empty()) {
    auto submitted = app.load_assets(*dependencies);
    if (!submitted)
      return submitted.error();
    output.visuals = std::move(*submitted);
  }
  scene_resource_bindings bindings;
  for (const auto& dependency : *dependencies) {
    auto loaded = app.assets().load(dependency);
    if (!loaded)
      return loaded.error();
    bindings.emplace(dependency.uri, loaded->resource());
  }
  auto destination = app.native_world()->create_scene();
  if (!destination)
    return destination.error();
  auto built = instantiate(*destination, *composed, bindings);
  if (!built) {
    (void)destination->unload();
    return built.error();
  }
  gneiss::entity_id camera;
  for (std::size_t i = 0; i < composed->objects.size(); ++i) {
    auto* description = composed->objects[i].effective.components.get<camera_description>();
    if (description && description->is_primary)
      camera = built->objects[i].entity;
  }
  if (camera) {
    auto selected = set_active_camera(*app.native_world(), camera);
    if (selected.failed()) {
      (void)destination->unload();
      return selected;
    }
  }
  output.instance = std::move(*built);
  return output;
}
/** 只领取已完成批次；视觉依赖失败保留场景并打印具体资产诊断。 */
inline void report_visuals(scene_activation& scene) {
  if (!scene.visuals || !scene.visuals.ready())
    return;
  auto completed = scene.visuals.take();
  if (completed) {
    for (const auto& item : completed->results)
      if (item.result.failed())
        std::fprintf(stderr, "视觉资产加载失败：%s (%d)\n", item.source.uri.c_str(),
                     item.result.code());
  }
  scene.visuals.reset();
}
}
