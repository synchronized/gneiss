// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "asset/resource_cache.h"
#include "asset/virtual_file_system.h"
#include "render/render_asset_loader.h"
#include "scene/prefab_asset_loader.h"
#include "scene/scene_instance_service.h"

namespace gneiss::application_internal {

/** 一个完整场景域；候选域未由 Application 发布前，所有公共查询仍访问旧域。
 * 仅所属线程创建/推进/销毁。缓存与 World 随域切换，底层渲染服务继续共享。
 * 旧帧持有不可变资源快照，域销毁不夺走已提交帧的数据所有权。 */
class application_scene_state final {
private:
  asset_internal::virtual_file_system files_;

public:
  application_scene_state(const asset_internal::virtual_file_system& files,
                          render_internal::render_resource_service& resources);
  ~application_scene_state() noexcept;
  application_scene_state(const application_scene_state&) = delete;
  application_scene_state& operator=(const application_scene_state&) = delete;
  [[nodiscard]] gneiss_result initialize() noexcept;

  asset_internal::resource_cache cache;
  render_internal::render_asset_loader assets;
  scene_internal::prefab_asset_loader prefabs;
  gneiss_world world{};
  std::unique_ptr<scene_internal::scene_instance_service> scenes;
};

} // namespace gneiss::application_internal
