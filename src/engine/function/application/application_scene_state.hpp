// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/application/application_asset_state.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"
#include "engine/function/scene/scene_instance_service.hpp"
#include "engine/function/world/world_service.hpp"

namespace gneiss::application_internal {

/** 一个完整场景域；候选域未由 Application 发布前，所有公共查询仍访问旧域。
 * 仅所属线程创建/推进/销毁。仅拥有 World 和场景实例；缓存、加载器借用
 * Application，必须比域更晚销毁。 旧帧持有不可变资源快照，域销毁不夺走已提交帧的数据所有权。 */
class application_scene_state final {
private:
  const asset_internal::virtual_file_system& files_;
  world_internal::world_owner world_owner_;

public:
  explicit application_scene_state(application_asset_state& owner);
  ~application_scene_state() noexcept;
  application_scene_state(const application_scene_state&) = delete;
  application_scene_state& operator=(const application_scene_state&) = delete;
  [[nodiscard]] gneiss_result initialize() noexcept;
  [[nodiscard]] engine::world* native_world() const noexcept { return world_owner_.get(); }

  asset_internal::resource_cache& cache;
  render_internal::render_asset_loader& assets;
  scene_internal::prefab_asset_loader& prefabs;
  gneiss_world world{};
  std::unique_ptr<scene_internal::scene_instance_service> scenes;
};

} // namespace gneiss::application_internal
